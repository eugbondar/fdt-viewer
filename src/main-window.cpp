#include <fdtviewer/main-window.hpp>
#include <fdtviewer/fdt/fdt-property-types.hpp>
#include "ui_main-window.h"

#include <QAction>
#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMessageBox>
#include <QTreeWidget>
#include <QKeyEvent>

#include <fdtviewer/dialogs.hpp>
#include <fdtviewer/endian-conversions.hpp>
#include <fdtviewer/fdt/fdt-parser.hpp>
#include <fdtviewer/fdt/fdt-view.hpp>
#include <fdtviewer/menu-manager.hpp>
#include <fdtviewer/viewer-settings.hpp>

#include <Qsci/qscilexercpp.h>
#include <Qsci/qsciscintilla.h>

#include "QHexView/qhexview.h"
#include "QHexView/model/buffer/qmemorybuffer.h"

using namespace Window;

MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent)
        , m_ui(std::make_unique<Ui::MainWindow>()) {
    setWindowIcon(QIcon(":/resources/fdt-viewer.svg"));
    m_ui->setupUi(this);
    m_ui->preview->setCurrentWidget(m_ui->text_view_page);
    m_ui->splitter->setEnabled(false);

    m_viewer = std::make_unique<fdt::viewer>(m_ui->treeWidget);

    m_hexview = new QHexView(this);
    m_hexview->setReadOnly(true);
    m_ui->hexview_layout->addWidget(m_hexview);

    m_ui->splitter->setStretchFactor(0, 2);
    m_ui->splitter->setStretchFactor(1, 5);

    m_menu = std::make_unique<menu_manager>(m_ui->menubar);
    connect(m_menu.get(), &menu_manager::show_full_screen, this, &MainWindow::showFullScreen);
    connect(m_menu.get(), &menu_manager::show_normal, this, &MainWindow::showNormal);
    connect(m_menu.get(), &menu_manager::quit, this, &MainWindow::close);

    m_ui->editor->setWrapMode(QsciScintilla::WrapNone);

    connect(m_menu.get(), &menu_manager::use_word_wrap, [this](const bool value) {
        m_ui->editor->setWrapMode(value ? QsciScintilla::WrapWord : QsciScintilla::WrapNone);
    });

    connect(m_menu.get(), &menu_manager::show_about_qt, []() { QApplication::aboutQt(); });
    connect(m_menu.get(), &menu_manager::open_file, this, [this]() {
        fdt::open_file_dialog(this, [this](auto &&...values) { open_file(std::forward<decltype(values)>(values)...); });
    });

    connect(m_menu.get(), &menu_manager::open_directory, this, [this]() {
        fdt::open_directory_dialog(this, [this](auto &&...values) { open_directory(std::forward<decltype(values)>(values)...); });
    });

    connect(m_ui->quick_search, &QLineEdit::textEdited, this, [this](const QString &text) {
        auto node = m_ui->treeWidget->invisibleRootItem();

        fdt::fdt_content_filter(
            node, [&text](const QString &value) -> bool {
                if (text.isEmpty())
                    return true;

                return value.indexOf(text) != -1;
            });

        update_view();

        if (!m_ui->treeWidget->selectedItems().empty())
            m_ui->treeWidget->scrollToItem(m_ui->treeWidget->selectedItems().first(), QAbstractItemView::PositionAtCenter);
    });

    connect(m_menu.get(), &menu_manager::quit, this, &MainWindow::close);
    connect(m_menu.get(), &menu_manager::close, this, [this]() {
        if (m_fdt) {
            m_viewer->drop(currentId());
            delete m_fdt;
            m_fdt = nullptr;
            update_view();
            save_last_opened();
        }
    });

    connect(m_menu.get(), &menu_manager::property_export, this, &MainWindow::property_export);
    connect(m_menu.get(), &menu_manager::close_all, this, [this]() {
        for (const auto& id: m_viewer->get_loaded())
            m_viewer->drop(id);
        m_fdt = nullptr;
        m_ui->treeWidget->clear();
        update_view();
        save_last_opened();
    });

    connect(m_ui->treeWidget, &QTreeWidget::itemSelectionChanged, this, &MainWindow::update_view);

    viewer_settings settings;
    auto lexer = new QsciLexerCPP(this, false);

    lexer->setFont(QFont{});
    lexer->setColor(Qt::yellow, QsciLexerCPP::Identifier);
    lexer->setColor(Qt::lightGray, QsciLexerCPP::Operator);
    lexer->setColor(Qt::green, QsciLexerCPP::DoubleQuotedString);

    m_ui->editor->setLexer(lexer);
    m_ui->editor->setMarginsBackgroundColor(Qt::black);
    m_ui->editor->setMarginsForegroundColor(Qt::green);
    m_ui->editor->setMarginType(0, QsciScintilla::NumberMargin);
    m_ui->editor->setMarginWidth(0, 50);

    if (settings.window_show_fullscreen.value())
        showFullScreen();

    auto rect = settings.window_position.value();

    if (!rect.isEmpty())
        setGeometry(rect);

    if (settings.view_darkstyle.value())
    {
        {
            QPalette palette;

            palette.setColor(QPalette::PlaceholderText, palette.color(QPalette::Inactive, QPalette::Text));
            this->setPalette(palette);
        }
        {
            for (int i = QsciLexerCPP::Default; i < QsciLexerCPP::InactiveEscapeSequence; i++)
                lexer->setPaper(QColor(0, 0, 0), i);

            m_ui->editor->setMarginsBackgroundColor(QColor(35, 35, 35));
            m_ui->editor->setMarginsForegroundColor(QColor(136, 136, 136));
            lexer->setDefaultPaper(QColor(0, 0, 0));
            lexer->setColor(QColor(170, 170, 170), QsciLexerCPP::Identifier);
            lexer->setColor(QColor(170, 170, 170), QsciLexerCPP::Operator);
            lexer->setColor(QColor(170, 170, 170), QsciLexerCPP::PreProcessor);
            lexer->setColor(QColor(255, 85, 255), QsciLexerCPP::DoubleQuotedString);
            lexer->setColor(QColor(255, 85, 255), QsciLexerCPP::Number);
            QFont font{};
            font.setBold(true);
            lexer->setFont(font, QsciLexerCPP::Identifier);
        }
        {
            QHexOptions opts;
            opts.linebackground = QColor(35, 35, 35);
            opts.headercolor = QColor(255, 255, 255);
            opts.separatorcolor = QColor(0, 150, 0);
            opts.commentcolor = QColor(136, 136, 136);

            m_hexview->setOptions(opts);
            {
                QPalette p = m_ui->property_view_page->palette();
                p.setColor(QPalette::ColorRole::Base, QColor(0, 0, 0));
                m_ui->property_view_page->setPalette(p);
            }
        }
    }

    if (settings.view_autoopen_lastloaded.value())
    {
        auto last_opened = settings.view_last_loaded.value();
        for (const auto& path: last_opened)
            open(path);
    }
}

MainWindow::~MainWindow() {
    viewer_settings settings;
    settings.window_position.set(geometry());
    save_last_opened();
}

void MainWindow::open_directory(const QString &path) {
    QDirIterator iter(path, {"*.dtb", "*.dtbo"}, QDir::Files);
    while (iter.hasNext()) {
        iter.next();
        open_file(iter.filePath());
    }
}

void MainWindow::open_file(const QString &path) {
    if (!open(path))
        dialogs::warn_invalid_fdt(path, this);
    else
        save_last_opened();
}

void MainWindow::save_last_opened()
{
    viewer_settings settings;
    settings.view_last_loaded.set(m_viewer->get_loaded());
}

bool MainWindow::open(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const auto info = QFileInfo(path);

    if (m_viewer->is_loaded(info.absoluteFilePath()) &&
        dialogs::ask_already_opened(this))
        return true;

    const auto ret = m_viewer->load(file.readAll(), info.fileName(), info.absoluteFilePath());
    update_view();
    return ret;
}

void MainWindow::update_fdt_path(QTreeWidgetItem *item) {
    m_menu->set_close_enabled(item);

    if (nullptr == item) {
        m_ui->path->clear();
        return;
    }

    QString path = item->text(0);
    auto root = item;
    while (root->parent()) {
        path = root->parent()->text(0) + "/" + path;
        root = root->parent();
    }

    m_fdt = root;

    m_ui->statusbar->showMessage("file://" + root->data(0, fdt::qt_wrappers::ROLE_FILEPATH).toString());
    m_ui->path->setText("fdt://" + path);
}

constexpr auto VIEW_TEXT_CACHE_SIZE = 1024 * 1024 * 32;

void MainWindow::update_view() {
    m_ui->splitter->setEnabled(m_ui->treeWidget->topLevelItemCount());
    m_menu->set_close_enabled(!m_ui->treeWidget->selectedItems().isEmpty());
    m_menu->set_close_all_enabled(m_ui->treeWidget->topLevelItemCount());

    if (m_ui->treeWidget->selectedItems().isEmpty()) {
        m_ui->preview->setCurrentWidget(m_ui->text_view_page);
        m_ui->editor->clear();
        m_ui->statusbar->clearMessage();
        m_ui->path->clear();
        return;
    }

    const auto item = m_ui->treeWidget->selectedItems().first();

    const auto type = item->data(0, fdt::qt_wrappers::ROLE_NODETYPE).value<NodeType>();
    m_ui->preview->setCurrentWidget(NodeType::Node == type ? m_ui->text_view_page : m_ui->property_view_page);

    if (NodeType::Property == type) {
        const auto property = item->data(0, fdt::qt_wrappers::ROLE_PROPERTY).value<fdt::qt_wrappers::property>();
        m_hexview->setDocument(QHexDocument::fromMemory<QMemoryBuffer>(property.data));
    }

    m_ui->editor->clear();
    update_fdt_path(item);

    QString ret;
    ret.reserve(VIEW_TEXT_CACHE_SIZE);
    fdt::fdt_view_dts(item, ret);

    m_ui->editor->setText(ret);

    viewer_settings settings;

    if (settings.view_darkstyle.value())
    {
        m_ui->editor->setMarginWidth(0, QString("_%0").arg(std::max(m_ui->editor->lines(), 9999)));
    }
    else
    {
        QFontMetrics metrics(QFont{});
        const auto num = QString::number(m_ui->editor->lines());

        m_ui->editor->setMarginWidth(0, metrics.horizontalAdvance(num) * 1.25);
    }
}

void MainWindow::property_export() {
    if (m_ui->treeWidget->selectedItems().isEmpty())
        return;

    const auto item = m_ui->treeWidget->selectedItems().first();
    const auto type = item->data(0, fdt::qt_wrappers::ROLE_NODETYPE).value<NodeType>();

    if (NodeType::Property == type) {
        const auto property = item->data(0, fdt::qt_wrappers::ROLE_PROPERTY).value<fdt::qt_wrappers::property>();

        m_hexview->setDocument(QHexDocument::fromMemory<QMemoryBuffer>(property.data));
        fdt::export_property_file_dialog(this, property.data, property.name);
    }
}

QString MainWindow::currentId()
{
    if (!m_fdt)
        return "";
    return m_fdt->data(0, fdt::qt_wrappers::ROLE_FILEPATH).toString();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // TODO: Maybe it's worth putting this into a class variable or a singleton? Loading the config every time is not very good.
    viewer_settings settings;
    if (settings.window_escape_exit.value() && event->key() == Qt::Key_Escape)
    {
        close();
    }
    else
    {
        QMainWindow::keyPressEvent(event);
    }
}

