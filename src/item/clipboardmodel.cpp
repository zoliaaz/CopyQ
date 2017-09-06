/*
    Copyright (c) 2017, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "clipboardmodel.h"

#include "common/contenttype.h"
#include "common/mimetypes.h"

#include <QStringList>

#include <algorithm>
#include <functional>

namespace {

QList<QPersistentModelIndex> validIndeces(const QModelIndexList &indexList)
{
    QList<QPersistentModelIndex> list;
    list.reserve(indexList.size());

    for (const auto &index : indexList) {
        if ( index.isValid() )
            list.append(index);
    }

    return list;
}

int topMostRow(const QList<QPersistentModelIndex> &indexList)
{
    int row = indexList.value(0).row();

    for (const auto &index : indexList)
        row = qMin(row, index.row());

    return row;
}

} // namespace

void ClipboardItemList::move(int from, int count, int to)
{
    if (to < from) {
        std::swap(from, to);
        ++to;
    }

    const auto start = std::begin(m_items) + from;
    const auto end = start + count;
    const auto dest = std::begin(m_items) + to;
    std::rotate(start, end, dest);
}

ClipboardModel::ClipboardModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ClipboardModel::rowCount(const QModelIndex&) const
{
    return m_clipboardList.size();
}

QVariant ClipboardModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_clipboardList.size())
        return QVariant();

    return m_clipboardList[index.row()].data(role);
}

Qt::ItemFlags ClipboardModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::ItemIsEnabled;

    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

bool ClipboardModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if ( !index.isValid() )
        return false;

    int row = index.row();

    if (role == Qt::EditRole) {
        m_clipboardList[row].setText(value.toString());
    } else if (role == contentType::notes) {
        const QString notes = value.toString();
        if ( notes.isEmpty() )
            m_clipboardList[row].removeData(mimeItemNotes);
        else
            m_clipboardList[row].setData( mimeItemNotes, notes.toUtf8() );
    } else if (role == contentType::updateData) {
        if ( !m_clipboardList[row].updateData(value.toMap()) )
            return false;
    } else if (role == contentType::data) {
        ClipboardItem &item = m_clipboardList[row];
        const QVariantMap dataMap = value.toMap();
        if ( !item.setData(dataMap) )
            return false;
    } else if (role >= contentType::removeFormats) {
        if ( !m_clipboardList[row].removeData(value.toStringList()) )
            return false;
    } else {
        return false;
    }

    emit dataChanged(index, index);

    return true;
}

void ClipboardModel::insertItem(const QVariantMap &data, int row)
{
    ClipboardItem item;
    item.setData(data);

    beginInsertRows(QModelIndex(), row, row);

    m_clipboardList.insert(row, item);

    endInsertRows();
}

void ClipboardModel::insertItems(const QList<QVariantMap> &dataList, int row)
{
    if ( dataList.isEmpty() )
        return;

    int targetRow = row;
    m_clipboardList.reserve( m_clipboardList.size() + dataList.size() );

    beginInsertRows(QModelIndex(), row, row + dataList.size() - 1);

    for ( auto it = std::begin(dataList); it != std::end(dataList); ++it ) {
        m_clipboardList.insert(targetRow, ClipboardItem(*it));
        ++targetRow;
    }

    endInsertRows();
}

bool ClipboardModel::insertRows(int position, int rows, const QModelIndex&)
{
    if ( rows <= 0 || position < 0 )
        return false;

    beginInsertRows(QModelIndex(), position, position + rows - 1);

    for (int row = 0; row < rows; ++row)
        m_clipboardList.insert(position, ClipboardItem());

    endInsertRows();

    return true;
}

bool ClipboardModel::removeRows(int position, int rows, const QModelIndex&)
{
    if ( rows <= 0 || position < 0 || position + rows > rowCount() )
        return false;

    int last = qMin( position + rows, rowCount() ) - 1;

    beginRemoveRows(QModelIndex(), position, last);

    m_clipboardList.remove(position, last - position + 1);

    endRemoveRows();

    return true;
}

bool ClipboardModel::moveRows(
        const QModelIndex &sourceParent, int sourceRow, int rows,
        const QModelIndex &destinationParent, int destinationRow)
{
    if (sourceParent.isValid() || destinationParent.isValid())
        return false;

    if (sourceRow < 0 || destinationRow < 0 || rows <= 0 || sourceRow + rows > rowCount())
        return false;

    const int last = sourceRow + rows - 1;
    if (sourceRow <= destinationRow && destinationRow <= last)
        return false;

    beginMoveRows(sourceParent, sourceRow, last, destinationParent, destinationRow);
    m_clipboardList.move(sourceRow, rows, destinationRow);
    endMoveRows();

    return true;
}

int ClipboardModel::getRowNumber(int row, bool cycle) const
{
    int n = rowCount();
    if (n == 0)
        return -1;

    if (row >= n)
        return cycle ? 0 : n - 1;

    if (row < 0)
        return cycle ? n - 1 : 0;

    return row;
}

bool ClipboardModel::move(int pos, int newpos)
{
    int from = getRowNumber(pos,true);
    int to   = getRowNumber(newpos,true);

    if( from == -1 || to == -1 )
        return false;

    const int sourceRow = from;
    const int targetRow = to;
    if (from < to)
        ++to;

    if ( !beginMoveRows(QModelIndex(), from, from, QModelIndex(), to) )
        return false;

    m_clipboardList.move(sourceRow, targetRow);

    endMoveRows();

    return true;
}

bool ClipboardModel::moveItemsWithKeyboard(const QModelIndexList &indexList, int key, int count) {
    int from, to;
    bool res = false;

    QList<int> list;
    list.reserve( indexList.size() );
    for (const auto &i : indexList)
        list.append( i.row() );

    if ( key == Qt::Key_Down || key == Qt::Key_End )
        std::sort( list.begin(), list.end(), std::greater<int>() );
    else
        std::sort( list.begin(), list.end(), std::less<int>() );

    for ( int i = 0, d = 0; i<list.length(); ++i ) {
        from = list.at(i) + d;

        switch (key) {
        case Qt::Key_Down:
            to = from + count;
            break;
        case Qt::Key_Up:
            to = from - count;
            break;
        case Qt::Key_End:
            to = rowCount()-i-1;
            break;
        default:
            to = 0+i;
            break;
        }

        if ( to < 0 )
            --d;
        else if (to >= rowCount() )
            ++d;

        if ( !move(from, to) )
            return false;
        if (!res)
            res = to==0 || from==0 || to == rowCount();
    }

    return res;
}

void ClipboardModel::sortItems(const QModelIndexList &indexList, CompareItems *compare)
{
    QList<QPersistentModelIndex> list = validIndeces(indexList);
    std::sort( list.begin(), list.end(), compare );

    int targetRow = topMostRow(list);

    for (const auto &ind : list) {
        if (ind.isValid()) {
            const int sourceRow = ind.row();

            if (targetRow != sourceRow) {
                beginMoveRows(QModelIndex(), sourceRow, sourceRow, QModelIndex(), targetRow);
                m_clipboardList.move(sourceRow, targetRow);
                endMoveRows();

                // If the moved item was removed or moved further (as reaction on moving the item),
                // stop sorting.
                if (!ind.isValid() || ind.row() != targetRow)
                    break;
            }

            ++targetRow;
        }
    }
}

int ClipboardModel::findItem(uint itemHash) const
{
    for (int i = 0; i < m_clipboardList.size(); ++i) {
        if ( m_clipboardList[i].dataHash() == itemHash )
            return i;
    }

    return -1;
}
