#pragma once

#include <fdtviewer/types.hpp>

#include <QMainWindow>
#include <memory>

#include <fdtviewer/fdt/fdt-view.hpp>

class QHexView;
class QTreeWidgetItem;
class menu_manager;

namespace Window {

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void open_file_dialog();
    void open_directory_dialog();

    void open_directory(const QString &path);
    void open_file(const QString &path);

    bool open(const QString &path);

private:
    void save_last_opened();
    void update_fdt_path(QTreeWidgetItem *item = nullptr);
    void update_view();
    void property_export();

protected:
    QString currentId();
    void keyPressEvent(QKeyEvent *event) override;

private:
    QHexView *m_hexview{nullptr};
    std::unique_ptr<Ui::MainWindow> m_ui;
    std::unique_ptr<menu_manager> m_menu;
    QTreeWidgetItem *m_fdt{nullptr};
    std::unique_ptr<fdt::viewer> m_viewer;
};

} // namespace Window
