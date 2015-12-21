// *****************************************************************************
//
// Copyright (c) 2015, Southwest Research Institute® (SwRI®)
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of Southwest Research Institute® (SwRI®) nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL Southwest Research Institute® BE LIABLE 
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
//
// *****************************************************************************
#include <swri_profiler_tools/partition_widget.h>

#include <QGraphicsView>
#include <QVBoxLayout>
#include <QDebug>

#include <swri_profiler_tools/util.h>
#include <swri_profiler_tools/profile_database.h>
#include <swri_profiler_tools/variant_animation.h>

namespace swri_profiler_tools
{
static QColor colorFromString(const QString &name)
{
  size_t name_hash = std::hash<std::string>{}(name.toStdString());
  
  int h = (name_hash >> 0) % 255;
  int s = (name_hash >> 8) % 200 + 55;
  int v = (name_hash >> 16) % 200 + 55;
  return QColor::fromHsv(h, s, v);
}

PartitionWidget::PartitionWidget(QWidget *parent)
  :
  QWidget(parent),
  db_(NULL)
{
  view_animator_ = new VariantAnimation(this);
  view_animator_->setEasingCurve(QEasingCurve::InOutCubic);
  QObject::connect(view_animator_, SIGNAL(valueChanged(const QVariant &)),
                   this, SLOT(update()));
}

PartitionWidget::~PartitionWidget()
{
}

void PartitionWidget::setDatabase(ProfileDatabase *db)
{
  if (db_) {
    // note(exjohnson): we can implement this later if desired, but
    // currently no use case for it.
    qWarning("PartitionWidget: Cannot change the profile database.");
    return;
  }

  db_ = db;

  update();

  QObject::connect(db_, SIGNAL(dataAdded(int)),    this, SLOT(updateData()));
  QObject::connect(db_, SIGNAL(profileAdded(int)), this, SLOT(updateData()));
  QObject::connect(db_, SIGNAL(nodesAdded(int)),   this, SLOT(updateData()));
}

void PartitionWidget::updateData()
{
  if (!active_key_.isValid()) {
    return;
  }    

  const Profile &profile = db_->profile(active_key_.profileKey());
  Layout layout = layoutProfile(profile);
  QRectF data_rect = dataRect(layout);
  view_animator_->setEndValue(data_rect);
  current_layout_ = layout;

  update();
}

void PartitionWidget::paintEvent(QPaintEvent *)
{
  QPainter painter(this);

  painter.setPen(Qt::NoPen);
  painter.fillRect(0, 0, width(), height(), QColor(255, 255, 255));
    
  if (current_layout_.empty()) {
    return;
  }

  const Profile &profile = db_->profile(active_key_.profileKey());

  QRectF data_rect = view_animator_->currentValue().toRectF();
  QRectF win_rect(QPointF(0, 0), QPointF(width(), height()));  
  QTransform win_from_data = getTransform(win_rect, data_rect);
  renderLayout(painter, win_from_data, current_layout_, profile);
}

QRectF PartitionWidget::dataRect(const Layout &layout) const
{
  QPointF tl(0, 0.0);
  QPointF br(layout.size(), 1.0);
  
  // This is a brute force search, we can be more intelligent about it
  // if necessary.
  for (size_t col = 0; col < layout.size(); col++) {
    for (size_t row = 0; row < layout[col].size(); row++) {
      const LayoutItem &item = layout[col][row];
      if (active_key_.nodeKey() == item.node_key) {
        const double rect_col = std::max(0.0, col-0.2);

        const double margin = 0.05;
        const double span_size = item.span_end - item.span_start;
        const double span_start = std::max(item.span_start - margin*span_size, 0.0);
        const double span_end = std::min(item.span_end + margin*span_size, 1.0);

        return QRectF(QPointF(rect_col, span_start),
                      QPointF(layout.size(), span_end));
      }
    }
  }

  qWarning("Active node key was not found in layout");
  return QRectF(QPointF(0, 0.0), QPointF(layout.size(), 1.0));
}

void PartitionWidget::setActiveNode(int profile_key, int node_key)
{
  const DatabaseKey new_key(profile_key, node_key);

  if (new_key == active_key_) {
    return;
  }

  bool first = true;
  if (active_key_.isValid()) {
    first = false;
  }
    
  active_key_ = new_key;
  
  const Profile &profile = db_->profile(active_key_.profileKey());
  Layout layout = layoutProfile(profile);
  QRectF data_rect = dataRect(layout);
  current_layout_ = layout;

  if (!first) {
    view_animator_->stop();
    view_animator_->setStartValue(view_animator_->endValue());
    view_animator_->setEndValue(data_rect);
    view_animator_->setDuration(500);
    view_animator_->start();
  } else {
    view_animator_->setStartValue(data_rect);
    view_animator_->setEndValue(data_rect);
  }    
}

PartitionWidget::Layout PartitionWidget::layoutProfile(const Profile &profile)
{
  Layout layout;
  
  const ProfileNode &root_node = profile.rootNode();
  if (!root_node.isValid()) {
    qWarning("Profile returned invalid root node.");
    return layout;
  }

  if (root_node.data().empty()) {
    return layout;
  }

  double time_scale = root_node.data().back().cumulative_inclusive_duration_ns;

  LayoutItem root_item;
  root_item.node_key = root_node.nodeKey();
  root_item.exclusive = false;
  root_item.span_start = 0.0;
  root_item.span_end = 1.0;
  layout.emplace_back();
  layout.back().push_back(root_item);

  bool keep_going = root_node.hasChildren();

  while (keep_going) {
    // We going to stop unless we see some children.
    keep_going = false;
    
    layout.emplace_back();
    const std::vector<LayoutItem> &parents = layout[layout.size()-2];
    std::vector<LayoutItem> &children = layout[layout.size()-1];
    
    double span_start = 0.0;
    for (auto const &parent_item : parents) {      
      const ProfileNode &parent_node = profile.node(parent_item.node_key);      
      
      // Add the carry-over exclusive item.
      {
        LayoutItem item;
        item.node_key = parent_item.node_key;
        item.exclusive = true;
        item.span_start = span_start;
        item.span_end = span_start + parent_node.data().back().cumulative_exclusive_duration_ns/time_scale;
        children.push_back(item);
        span_start = item.span_end;
      }

      // Don't add children for an exclusive item because they've already been added.
      if (parent_item.exclusive) {
        continue;
      }
      
      for (int child_key : parent_node.childKeys()) {
        const ProfileNode &child_node = profile.node(child_key);
        
        LayoutItem item;
        item.node_key = child_key;
        item.exclusive = false;
        item.span_start = span_start;
        item.span_end = span_start + child_node.data().back().cumulative_inclusive_duration_ns / time_scale;
        children.push_back(item);
        span_start = item.span_end;

        keep_going |= child_node.hasChildren();
      }
    }
  }

  return layout;
}


void PartitionWidget::renderLayout(QPainter &painter,
                                   const QTransform &win_from_data,
                                   const Layout &layout,
                                   const Profile &profile)
{
  // Set painter to use a single-pixel black pen.
  painter.setPen(Qt::black);  

  for (size_t col = 0; col < layout.size(); col++) {
    for (size_t row = 0; row < layout[col].size(); row++) {
      const LayoutItem &layout_item = layout[col][row];

      if (layout_item.exclusive) {
         continue;
      }
      
      const ProfileNode &node = profile.node(layout_item.node_key);
      QColor color = colorFromString(node.name());

      QPointF tl(col, layout_item.span_start);
      QPointF br(layout.size(), layout_item.span_end);
      QRectF data_rect(tl, br);
      QRectF win_rect = win_from_data.mapRect(data_rect);

      QRect int_rect = win_rect.toRect();
      QRect int_rect2 = roundRectF(win_rect);

      painter.setBrush(color);
      painter.drawRect(int_rect2.adjusted(0,0,-1,-1));
    }
  } 
}

QTransform PartitionWidget::getTransform(const QRectF &win_rect,
                                         const QRectF &data_rect)
{
  double sx = win_rect.width() / data_rect.width();
  double sy = win_rect.height() / data_rect.height();
  double tx = win_rect.topLeft().x() - sx*data_rect.topLeft().x();
  double ty = win_rect.topLeft().y() - sy*data_rect.topLeft().y();
  
  QTransform win_from_data(sx, 0.0, 0.0,
                           0.0, sy, 0.0,
                           tx, ty, 1.0);
  return win_from_data;
}
}  // namespace swri_profiler_tools
