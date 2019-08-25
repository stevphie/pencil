/*

Pencil - Traditional Animation Software
Copyright (C) 2005-2007 Patrick Corrieri & Pascal Naidon
Copyright (C) 2012-2018 Matthew Chiawen Chang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*/

#include "bitmappolylinetool.h"


#include "editor.h"
#include "scribblearea.h"

#include "strokemanager.h"
#include "layermanager.h"
#include "colormanager.h"
#include "viewmanager.h"
#include "pointerevent.h"
#include "layerbitmap.h"
#include "beziercurve.h"

BitmapPolylineTool::BitmapPolylineTool(QObject* parent) : BaseTool(parent)
{
}

QCursor BitmapPolylineTool::cursor()
{
    return Qt::CrossCursor;
}

ToolType BitmapPolylineTool::type()
{
    return POLYLINE;
}

void BitmapPolylineTool::loadSettings()
{
    mPropertyEnabled[WIDTH] = true;
    mPropertyEnabled[BEZIER] = true;
    mPropertyEnabled[ANTI_ALIASING] = true;

    QSettings settings(PENCIL2D, PENCIL2D);

    properties.width = settings.value("polyLineWidth", 8.0).toDouble();
    properties.pressure = false;
    properties.useAA = settings.value("brushAA").toBool();
}

void BitmapPolylineTool::resetToDefault()
{
    setWidth(8.0);
    setBezier(false);
}

void BitmapPolylineTool::setWidth(const qreal width)
{
    // Set current property
    properties.width = width;

    // Update settings
    QSettings settings(PENCIL2D, PENCIL2D);
    settings.setValue("polyLineWidth", width);
    settings.sync();
}

void BitmapPolylineTool::setFeather(const qreal feather)
{
    Q_UNUSED(feather);
    properties.feather = -1;
}

void BitmapPolylineTool::setAA(const int AA)
{
    // Set current property
    properties.useAA = AA;

    // Update settings
    QSettings settings(PENCIL2D, PENCIL2D);
    settings.setValue("brushAA", AA);
    settings.sync();
}

void BitmapPolylineTool::clearToolData()
{
    mPoints.clear();
}

void BitmapPolylineTool::pointerPressEvent(PointerEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        mScribbleArea->handleDrawingOnEmptyFrame();

        mPoints << getCurrentPoint();
        mScribbleArea->setAllDirty();
    }
}

void BitmapPolylineTool::pointerMoveEvent(PointerEvent*)
{
    drawPolyline(mPoints, getCurrentPoint());
}

void BitmapPolylineTool::pointerReleaseEvent(PointerEvent *)
{}

void BitmapPolylineTool::pointerDoubleClickEvent(PointerEvent*)
{
    // include the current point before ending the line.
    mPoints << getCurrentPoint();

    mEditor->backup(typeName());

    endPolyline(mPoints);
    clearToolData();
}

bool BitmapPolylineTool::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
    case Qt::Key_Return:
        if (mPoints.size() > 0)
        {
            endPolyline(mPoints);
            clearToolData();
            return true;
        }
        break;

    case Qt::Key_Escape:
        if (mPoints.size() > 0)
        {
            cancelPolyline();
            clearToolData();
            return true;
        }
        break;

    default:
        return false;
    }

    return false;
}

void BitmapPolylineTool::drawPolyline(QList<QPointF> points, QPointF endPoint)
{
    if (points.size() > 0)
    {
        QPen pen(mEditor->color()->frontColor(),
                 properties.width,
                 Qt::SolidLine,
                 Qt::RoundCap,
                 Qt::RoundJoin);

        QPainterPath tempPath;
        if (properties.bezier_state)
        {
            tempPath = BezierCurve(points).getSimplePath();
        }
        else
        {
            tempPath = BezierCurve(points).getStraightPath();
        }
        tempPath.lineTo(endPoint);

        mScribbleArea->drawPolyline(tempPath, pen, properties.useAA);
    }
}


void BitmapPolylineTool::cancelPolyline()
{
    // Clear the in-progress polyline from the bitmap buffer.
    mScribbleArea->clearBitmapBuffer();
    mScribbleArea->updateCurrentFrame();
}

void BitmapPolylineTool::endPolyline(QList<QPointF> points)
{
    Layer* layer = mEditor->layers()->currentLayer();

    drawPolyline(points, points.last());
    BitmapImage *bitmapImage = static_cast<LayerBitmap*>(layer)->getLastBitmapImageAtFrame(mEditor->currentFrame(), 0);
    bitmapImage->paste(mScribbleArea->mBufferImg);

    mScribbleArea->mBufferImg->clear();
    mScribbleArea->setModified(mEditor->layers()->currentLayerIndex(), mEditor->currentFrame());
}
