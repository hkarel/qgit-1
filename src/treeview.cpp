/*
    Description: files tree view

    Author: Marco Costalba (C) 2005-2007

    Copyright: See COPYING file that comes with this distribution

*/
#include "git.h"
#include "domain.h"
#include "mainimpl.h"
#include "treeview.h"

#include "shared/defmac.h"

#include <QApplication>
#include <QTreeWidgetItemIterator>

QString FileItem::fullName() const {

    QTreeWidgetItem* p = parent();
    QString s(p ? text(0) : ""); // root directory has no fullName
    while (p && p->parent()) {
        s.prepend(p->text(0) + '/');
        p = p->parent();
    }
    return s;
}

void FileItem::setBold(bool b) {

    if (font(0).bold() == b)
        return;

    QFont fnt(font(0));
    fnt.setBold(b);
    setFont(0, fnt);
}

DirItem::DirItem(DirItem* p, const QString& ts, const QString& nm) : FileItem(p, nm), treeSha(ts) {}
DirItem::DirItem(QTreeWidget* p, const QString& ts, const QString& nm) : FileItem(p, nm), treeSha(ts) {}

void TreeView::setup(Domain* dm, Git* g) {

    d = dm;
    git = g;
    st = &(d->st);
    ignoreCurrentChanged = false;
    isWorkingDir = false;

    // set built-in pixmaps
    folderClosed = qgit::mimePix(".#folder_closed");
    folderOpen   = qgit::mimePix(".#folder_open");
    fileDefault  = qgit::mimePix(".#default");

    chk_connect_a(this, SIGNAL(itemExpanded(QTreeWidgetItem*)),
                  this, SLOT(on_itemExpanded(QTreeWidgetItem*)));

    chk_connect_a(this, SIGNAL(itemCollapsed(QTreeWidgetItem*)),
                  this, SLOT(on_itemCollapsed(QTreeWidgetItem*)));

    chk_connect_a(this, SIGNAL(currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)),
                  this, SLOT(on_currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)));

    chk_connect_a(this, SIGNAL(customContextMenuRequested(const QPoint&)),
                  this, SLOT(on_customContextMenuRequested(const QPoint&)));
}

void TreeView::on_currentItemChanged(QTreeWidgetItem* item, QTreeWidgetItem*) {

    if (item) {
        const QString& fn = ((FileItem*)item)->fullName();
        if (!ignoreCurrentChanged && fn != st->fileName()) {
            st->setFileName(fn);
            st->setSelectItem(true);
            UPDATE_DOMAIN(d);
        }
    }
}

void TreeView::on_customContextMenuRequested(const QPoint& pos) {

    QTreeWidgetItem* item = itemAt(pos);
    if (item)
        emit contextMenu(fullName(item), qgit::POPUP_TREE_EV);
}

void TreeView::clear() {

    rootName = "";
    QTreeWidget::clear();
}

bool TreeView::isModified(const QString& path, bool isDir) {

    if (isDir)
        return modifiedDirs.contains(path);

    return modifiedFiles.contains(path);
}

bool TreeView::isDir(const QString& fileName) {

    // if currentItem is NULL or is different from fileName
    // return false, because treeview is not updated while
    // not visible, so could be out of sync.
    FileItem* item = static_cast<FileItem*>(currentItem());
    if (item == NULL || item->fullName() != fileName)
        return false;

    return dynamic_cast<DirItem*>(item);
}

const QString TreeView::fullName(QTreeWidgetItem* item) {

    FileItem* f = static_cast<FileItem*>(item);
    return (item ? f->fullName() : "");
}

void TreeView::getTreeSelectedItems(QStringList& selectedItems) {

    selectedItems.clear();
    QList<QTreeWidgetItem*> ls = QTreeWidget::selectedItems();
    for (QTreeWidgetItem* twi : ls) {
        FileItem* f = static_cast<FileItem*>(twi);
        selectedItems.append(f->fullName());
    }
}

void TreeView::setTree(const QString& treeSha) {

    if (topLevelItemCount() == 0)
                // get working directory info only once after each TreeView::clear()
        git->getWorkDirFiles(modifiedFiles, modifiedDirs, RevFile::ANY);

    QTreeWidget::clear();
    treeIsValid = true;

    if (!treeSha.isEmpty()) {
        // insert a new dir at the beginning of the list
        DirItem* root = new DirItem(this, treeSha, rootName);
        expandItem(root); // be interesting
    }
}

bool TreeView::getTree(const QString& treeSha, Git::TreeInfo& ti, bool wd, const QString& treePath) {

    // calls qApp->processEvents()
    treeIsValid = git->getTree(treeSha, ti, wd, treePath);
    return treeIsValid;
}

void TreeView::on_itemCollapsed(QTreeWidgetItem* item) {

    item->setData(0, Qt::DecorationRole, *folderClosed);
}

void TreeView::on_itemExpanded(QTreeWidgetItem* itm) {

    DirItem* item = dynamic_cast<DirItem*>(itm);
    if (!item)
        return;

    item->setData(0, Qt::DecorationRole, *folderOpen);

    bool alreadyWaiting = false;
    if (QApplication::overrideCursor())
        alreadyWaiting = (QApplication::overrideCursor()->shape() == Qt::WaitCursor);

    if (!alreadyWaiting)
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    if (item->childCount() < 2) {

        QTreeWidgetItem* dummy = item->child(0);
        if (dummy && dummy->text(0).isEmpty())
            delete dummy; // remove dummy child

        Git::TreeInfo ti;
        if (!getTree(item->treeSha, ti, isWorkingDir, item->fullName()))
            return;

        if (!ti.empty()) {

            Git::TreeInfo::const_iterator it(ti.constBegin());
            while (it != ti.constEnd()) {

                const Git::TreeEntry& te = *it;

                if (te.type == "tree") {
                    DirItem* dir = new DirItem(item, te.sha, te.name);
                    dir->setData(0, Qt::DecorationRole, *folderClosed);
                    dir->setBold(isModified(dir->fullName(), true));
                    new DirItem(dir, "", ""); // dummy child to show expand sign
                } else {
                    FileItem* file = new FileItem(item, te.name);
                    file->setData(0, Qt::DecorationRole, *qgit::mimePix(te.name));
                    file->setBold(isModified(file->fullName()));
                }
                ++it;
            }
        }
    }
    if (!alreadyWaiting)
        QApplication::restoreOverrideCursor();
}

void TreeView::updateTree() {

    if (st->sha().isEmpty())
        return;

    // qt emits currentChanged() signal when populating
    // the list view, so we should ignore while here
    ignoreCurrentChanged = true;
    QApplication::setOverrideCursor(Qt::WaitCursor);

    isWorkingDir = (st->sha() == qgit::ZERO_SHA);
    bool newTree = true;
    DirItem* root = static_cast<DirItem*>(topLevelItem(0));
    if (root && treeIsValid)
        newTree = (root->treeSha != st->sha());

    if (   newTree
        && treeIsValid
        && root
        && st->sha() != qgit::ZERO_SHA
        && root->treeSha != qgit::ZERO_SHA) {
        // root->treeSha could reference a different sha from current
        // one in case the tree is the same, i.e. has the same files.
        // so we prefer to use the previous state sha to call isSameFiles()
        // and benefit from the early skip logic.
        // In case previous sha is the same of current it means an update
        // call has been forced, in that case we use the 'real' root->treeSha
        if (st->sha(true) != st->sha(false))
            newTree = !git->isSameFiles(st->sha(false), st->sha(true));
        else
            newTree = !git->isSameFiles(root->treeSha, st->sha(true));
    }

    if (newTree) // ok, we really need to update the tree
        setTree(st->sha());
    else {
        FileItem* f = static_cast<FileItem*>(currentItem());
        if (f && f->fullName() == st->fileName()) {

            restoreStuff();
            return;
        }
    }
    if (st->fileName().isEmpty()) {
        restoreStuff();
        return;
    }
    setUpdatesEnabled(false);
    const QStringList lst {st->fileName().split("/")};

    QTreeWidgetItemIterator item(this);
    ++item; // first item is repository name
    for (const QString& s : lst) {
        while (*item && treeIsValid) {

            if ((*item)->text(0) == s) {

                // could be a different subdirectory with the
                // same name that appears before in tree view
                // to be sure we need to check the names
                const QString& fn = static_cast<FileItem*>(*item)->fullName();
                if (st->fileName().startsWith(fn)) {

                    if (dynamic_cast<DirItem*>(*item)) {
                        expandItem(*item);
                        ++item;
                    }
                    break; // from while loop only
                }
            }
            ++item;
        }
    }
    // check if st->fileName() has been deleted by a patch older than this tree
    if (*item && treeIsValid) {
        clearSelection();
        setCurrentItem(*item); // calls on_currentChanged()
        scrollToItem(*item);
    }
    setUpdatesEnabled(true);
    QTreeWidget::update();
    restoreStuff();
}

void TreeView::restoreStuff() {

    ignoreCurrentChanged = false;
    QApplication::restoreOverrideCursor();
}
