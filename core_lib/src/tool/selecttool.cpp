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
#include "selecttool.h"
#include "pointerevent.h"
#include "vectorimage.h"
#include "editor.h"
#include "strokemanager.h"
#include "layervector.h"
#include "scribblearea.h"
#include "backupmanager.h"
#include "layermanager.h"
#include "toolmanager.h"
#include "selectionmanager.h"

SelectTool::SelectTool(QObject* parent) : BaseTool(parent)
{
}

void SelectTool::loadSettings()
{
    properties.width = -1;
    properties.feather = -1;
    properties.stabilizerLevel = -1;
    properties.useAA = -1;
}

QCursor SelectTool::cursor()
{
    MoveMode mode = mEditor->select()->getMoveModeForSelectionAnchor(getCurrentPoint());
    return this->selectMoveCursor(mode, type());
}

void SelectTool::beginSelection()
{
    // Store original click position for help with selection rectangle.
    mAnchorOriginPoint = getLastPoint();

    auto selectMan = mEditor->select();

    // checks whether anchorPoint and selection is still valid
    // otherwise make sure selection will be deselected on release
    if (selectMan->somethingSelected()) {
        if (maybeDeselect()) {
            mDeselectSelection = true;
        }
    }

    QPointF lastPoint = getLastPoint();
    QPointF currentPoint = getCurrentPoint();

    if (mCurrentLayer->type() == Layer::BITMAP) {
        lastPoint = lastPoint.toPoint();
        mAnchorOriginPoint = lastPoint;
        currentPoint = currentPoint.toPoint();
    }

    mEditor->backups()->saveStates();

    selectMan->calculateSelectionTransformation();

    // paint and apply the transformation
    mScribbleArea->paintTransformedSelection();
    mScribbleArea->applyTransformedSelection();

    if (selectMan->somethingSelected()) // there is something selected
    {
        if (!selectMan->myTempTransformedSelectionRect().contains(getCurrentPoint())) {
            mPointOutsideSelection = true;
        }
        if (mCurrentLayer->type() == Layer::VECTOR)
        {
            static_cast<LayerVector*>(mCurrentLayer)->getLastVectorImageAtFrame(mEditor->currentFrame(), 0)->deselectAll();
        }

        mAnchorOriginPoint = selectMan->whichAnchorPoint(lastPoint);

        // the user did not click on one of the corners
        if (selectMan->validateMoveMode(lastPoint) == MoveMode::NONE)
        {
            QRectF newRect = QRectF(lastPoint, lastPoint);
            selectMan->setSelection(newRect);
        }
    }
    else
    {
        selectMan->setSelection(QRectF(currentPoint.x(), currentPoint.y(),1,1));
        mMoveMode = MoveMode::NONE;
    }
    mScribbleArea->update();
}

void SelectTool::pointerPressEvent(PointerEvent* event)
{
    mCurrentLayer = mEditor->layers()->currentLayer();
    if (mCurrentLayer == nullptr) return;
    if (!mCurrentLayer->isPaintable()) { return; }
    if (event->button() != Qt::LeftButton) { return; }
    auto selectMan = mEditor->select();

    mMoveMode = selectMan->validateMoveMode(getCurrentPoint());

    selectMan->updatePolygons();

    beginSelection();
}

void SelectTool::pointerMoveEvent(PointerEvent* event)
{
    mCurrentLayer = mEditor->layers()->currentLayer();
    if (mCurrentLayer == nullptr) { return; }
    if (!mCurrentLayer->isPaintable()) { return; }
    auto selectMan = mEditor->select();

    if (!selectMan->somethingSelected()) { return; }

    QPointF currentPoint = getCurrentPoint();

    if (mCurrentLayer->type() == Layer::BITMAP) {
        currentPoint = currentPoint.toPoint();
    }

    selectMan->updatePolygons();

    mScribbleArea->updateToolCursor();

    if (mScribbleArea->isPointerInUse())
    {
        controlOffsetOrigin(currentPoint, mAnchorOriginPoint);

        if (mCurrentLayer->type() == Layer::VECTOR)
        {
            static_cast<LayerVector*>(mCurrentLayer)->
                    getLastVectorImageAtFrame(mEditor->currentFrame(), 0)->
                    select(selectMan->myTempTransformedSelectionRect());
        }
    }

    mScribbleArea->updateCurrentFrame();

}

void SelectTool::pointerReleaseEvent(PointerEvent* event)
{
    mCurrentLayer = mEditor->layers()->currentLayer();
    if (mCurrentLayer == nullptr) return;
    if (event->button() != Qt::LeftButton) return;
    auto selectMan = mEditor->select();

    mMoveMode = MoveMode::NONE;

    // if there's a small very small distance between current and last point
    // discard the selection...
    // TODO: improve by adding a timer to check if the user is deliberately selecting
    if (QLineF(mAnchorOriginPoint, getCurrentPoint()).length() < 5.0)
    {
        mEditor->deselectAll();
        mDeselectSelection = false;
        // no backup here, since we didn't intend to make a selection in the first place
    }

    if (mDeselectSelection)
    {
        mEditor->deselectAll();
        mEditor->backups()->deselect();
        mDeselectSelection = false;
    }
    else
    {
        keepSelection();
    }

    selectMan->updatePolygons();

    mScribbleArea->updateToolCursor();
    mScribbleArea->updateCurrentFrame();
    //mScribbleArea->setAllDirty();
}

bool SelectTool::maybeDeselect()
{
    return (!isSelectionPointValid() &&
                mEditor->select()->validateMoveMode(getLastPoint()) == MoveMode::NONE);
}

/**
 * @brief SelectTool::keepSelection
 * Keep selection rect and normalize if invalid
 */
void SelectTool::keepSelection()
{
    auto selectMan = mEditor->select();
    if (mCurrentLayer->type() == Layer::BITMAP) {
        if (!selectMan->myTempTransformedSelectionRect().isValid())
        {
            selectMan->setSelection(selectMan->myTempTransformedSelectionRect().normalized());
        }
        else
        {
            selectMan->setSelection(selectMan->myTempTransformedSelectionRect());
        }
    }
    else if (mCurrentLayer->type() == Layer::VECTOR)
    {
        VectorImage* vectorImage = static_cast<LayerVector*>(mCurrentLayer)->getLastVectorImageAtFrame(mEditor->currentFrame(), 0);
        selectMan->setSelection(vectorImage->getSelectionRect());
        selectMan->addCurvesToVectorSelection(vectorImage->getSelectedCurveNumbers());
    }

    mEditor->backups()->selection();
}

void SelectTool::controlOffsetOrigin(QPointF currentPoint, QPointF anchorPoint)
{
    QPointF offset = offsetFromPressPos();

    if (mMoveMode != MoveMode::NONE)
    {
        QPointF currentPoint = getCurrentPoint();
        if (editor()->layers()->currentLayer()->type() == Layer::BITMAP) {
            offset = QPointF(offset).toPoint();
            currentPoint = currentPoint.toPoint();
            auto selectMan = mEditor->select();

            selectMan->adjustSelection(currentPoint, offset.x(), offset.y(), selectMan->myRotation(), 0);
        }
    }
    else
    {
        // when the selection is none, manage the selection Origin
        manageSelectionOrigin(currentPoint, anchorPoint);
    }
}

/**
 * @brief SelectTool::manageSelectionOrigin
 * switches anchor point when crossing threshold
 */
void SelectTool::manageSelectionOrigin(QPointF currentPoint, QPointF originPoint)
{
    qreal mouseX = currentPoint.x();
    qreal mouseY = currentPoint.y();

    QRectF selectRect;

    if (mouseX <= originPoint.x())
    {
        selectRect.setLeft(mouseX);
        selectRect.setRight(originPoint.x());
    }
    else
    {
        selectRect.setLeft(originPoint.x());
        selectRect.setRight(mouseX);
    }

    if (mouseY <= originPoint.y())
    {
        selectRect.setTop(mouseY);
        selectRect.setBottom(originPoint.y());
    }
    else
    {
        selectRect.setTop(originPoint.y());
        selectRect.setBottom(mouseY);
    }

    mEditor->select()->setTempTransformedSelectionRect(selectRect);
}

bool SelectTool::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
    case Qt::Key_Alt:
        mScribbleArea->setTemporaryTool(MOVE);
        break;
    default:
        break;
    }

    // Follow the generic behaviour anyway
    return false;
}

QPointF SelectTool::offsetFromPressPos()
{
    return getCurrentPoint() - getCurrentPressPoint();
}
