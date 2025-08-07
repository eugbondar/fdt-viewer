#include <fdtviewer/fdt/fdt-view.hpp>

#include <fdtviewer/endian-conversions.hpp>
#include <fdtviewer/fdt/fdt-generator-qt.hpp>
#include <fdtviewer/fdt/fdt-parser.hpp>
#include <cstddef>

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QFileInfo>

#include <fdtviewer/fdt/fdt-parser-tokens.hpp>
#include <fdtviewer/fdt/fdt-property-types.hpp>
#include "qnamespace.h"
#include <string_view>

namespace {
constexpr auto BINARY_PREVIEW_LIMIT = 64;
constexpr auto DEBUG_LINENUMBER = false;

QString present_u32be(const QByteArray &data) {
    QString ret;

    auto array = reinterpret_cast<u8 *>(const_cast<char *>(data.data()));
    for (auto i = 0; i < data.size(); ++i) {
        ret += "0x" + QString::number(array[i], 16).rightJustified(2, '0').toUpper() + " ";
        if (i == BINARY_PREVIEW_LIMIT) {
            ret += "... ";
            break;
        }
    }
    ret.remove(ret.size() - 1, 1);

    return ret;
}

QString present(const fdt::qt_wrappers::property &p) {
    auto &name = p.name;
    auto &data = p.data;

    auto result = [&](QString &&value) {
        return name + " = <" + value + ">;";
    };

    auto result_str = [&](QString &&value) {
        return name + " = \"" + value + "\";";
    };

    if (property_map.contains(name)) {
        const property_info info = property_map.value(name);
        if (property_type::string == info.type)
            return result_str(QString(data.data()));

        if (property_type::number == info.type)
            return result(QString::number(byteorder(*reinterpret_cast<const u32 *>(data.data()))));
    }

    const static QRegularExpression cells_regexp("^#.*-cells$");
    const static QRegularExpression names_regexp("^.*-names");

    if (cells_regexp.match(name).hasMatch())
        return result(QString::number(byteorder(*reinterpret_cast<const u32 *>(data.data()))));

    if (names_regexp.match(name).hasMatch()) {
        auto lines = data.split(0);
        lines.removeLast();

        QString ret;
        for (auto i = 0; i < lines.count(); ++i) {
            if (i == lines.count() - 1)
                ret += lines[i];
            else
                ret += lines[i] + ", ";
        }

        return result_str(std::move(ret));
    }

    if (std::count_if(data.begin(), data.end(), [](auto &&value) { return value == 0x00; }) == 1 &&
        data.back() == 0x00) return result_str(data.data());

    return result(present_u32be(data));
}
} // namespace

fdt::viewer::viewer(QTreeWidget *target)
        : m_target(target) {
}

auto fdt::viewer::is_loaded(QString &&id) const noexcept -> bool {
    return m_tree.contains(id);
}

auto fdt::viewer::is_loaded(const QString &id) const noexcept -> bool {
    return m_tree.contains(id);
}

auto fdt::viewer::get_loaded() const noexcept -> QStringList
{
    const auto& keys = m_tree.keys();
    return QStringList(keys.begin(), keys.end());
}

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

bool fdt::viewer::load(QByteArray &&data, QString &&name, QString &&id) {
    using namespace fdt::parser;
    using namespace fdt::qt_wrappers;

    auto results = parse_multiple_offsets({data.data(), static_cast<std::size_t>(data.size())});

    for (auto &&result : results) {
        if (!result)
            return false;

        auto &&tokens = result.value().tokens;

        QString root_id = id;
        QString root_name = name;

        if (result->offset) {
            root_name += "@" + QString::number(result->offset, 16);
            root_id += "@" + QString::number(result->offset, 16);
        }

        fdt::parser::rename_root(tokens, root_name.toStdString());

        m_target->blockSignals(true);
        tree_generator generator(m_tree[root_id], m_target, std::move(root_name), std::move(id));
        m_target->blockSignals(false);

        for (auto &&token : tokens)
            std::visit(overloaded{
                           [&](const token_types::node_begin &arg) { generator.begin_node(arg.name); },
                           [&](const token_types::node_end &) { generator.end_node(); },
                           [&](const token_types::property &arg) { generator.insert_property(arg); },
                           [&](const token_types::nop &) {},
                           [&](const token_types::end &) {},
                       },
                token);

        generator.root()->setData(0, Qt::UserRole + 1000, std::move(data));
    }

    return true;
}

void fdt::viewer::drop(const QString &&id) {
    m_tree.remove(id);
}

void fdt::viewer::drop(const QString &id) {
    m_tree.remove(id);
}

bool fdt::fdt_content_filter(QTreeWidgetItem *node, const std::function<bool(const QString &)> &match) {
    QList<QTreeWidgetItem *> nodes;
    QList<QTreeWidgetItem *> properties;

    for (auto i = 0; i < node->childCount(); ++i) {
        const auto child = node->child(i);
        switch (child->data(0, fdt::qt_wrappers::ROLE_NODETYPE).value<NodeType>()) {
            case NodeType::Node:
                nodes.append(child);
                break;
            case NodeType::Property:
                properties.append(child);
                break;
        }
    }

    const auto name = node->data(0, Qt::DisplayRole).toString();

    bool isFound = match(name);

    for (auto item : properties) {
        if (isFound)
            break;

        const auto property = item->data(0, fdt::qt_wrappers::ROLE_PROPERTY).value<fdt::qt_wrappers::property>();
        isFound |= match(property.name) || match(present(property));
    }

    for (auto i = 0; i < nodes.count(); ++i) {
        isFound |= fdt::fdt_content_filter(nodes.at(i), match);
    }

    node->setHidden(!isFound);

    return isFound;
}

bool fdt::fdt_view_dts(QTreeWidgetItem *item, QString &ret, int depth, int *line_number_counter_) {
    QString depth_str;
    depth_str.fill(' ', depth * 4);

    QList<QTreeWidgetItem *> nodes;
    QList<QTreeWidgetItem *> properties;

    for (auto i = 0; i < item->childCount(); ++i) {
        const auto child = item->child(i);
        switch (child->data(0, fdt::qt_wrappers::ROLE_NODETYPE).value<NodeType>()) {
            case NodeType::Node:
                nodes.append(child);
                break;
            case NodeType::Property:
                properties.append(child);
                break;
        }
    }

    if (item->isHidden())
        return false;

    int line_number_counter = line_number_counter_ ? *line_number_counter_ : 1;

    if (DEBUG_LINENUMBER) ret += QVariant(line_number_counter).toString() + " ";
    ret += depth_str + item->data(0, Qt::DisplayRole).toString() + " {\n";
    item->setData(0, fdt::qt_wrappers::ROLE_LINENUMBER, line_number_counter++);

    for (auto item : properties) {
        const auto property = item->data(0, fdt::qt_wrappers::ROLE_PROPERTY).value<fdt::qt_wrappers::property>();
        if (DEBUG_LINENUMBER) ret += QVariant(line_number_counter).toString() + " ";
        ret += depth_str + "    " + present(property) + "\n";
        item->setData(0, fdt::qt_wrappers::ROLE_LINENUMBER, line_number_counter++);
    }

    if (!properties.isEmpty() && !nodes.isEmpty())
    {
        ret += "\n";
        line_number_counter++;
    }

    for (auto i = 0; i < nodes.count(); ++i) {
        if (!fdt_view_dts(nodes.at(i), ret, depth + 1, &line_number_counter))
            continue;

        if (nodes.count() - 1 != i)
        {
            ret += "\n";
            line_number_counter++;
        }
    }

    ret += depth_str + "};\n";
    line_number_counter++;

    if (line_number_counter_)
        *line_number_counter_ = line_number_counter;

    return true;
}
