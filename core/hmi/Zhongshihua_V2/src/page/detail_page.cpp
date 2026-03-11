#include "detail_page.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QPalette>

DetailPage::DetailPage(QWidget* parent)
    : QWidget(parent)
{
    // BY ZF: 详情页先预留骨架，后续承接计费模型/调试信息/记录/外置存储。
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 56, 24, 24);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(20, 30, 44));
    pal.setColor(QPalette::WindowText, QColor(230, 238, 246));
    setAutoFillBackground(true);
    setPalette(pal);

    QLabel* title = new QLabel(QString::fromUtf8("详情页（预留）"), this);
    QLabel* body = new QLabel(QString::fromUtf8(
        "后续将在此承载：\n"
        "1) 计费模型\n"
        "2) 调试信息\n"
        "3) 充电记录\n"
        "4) 故障记录\n"
        "5) 外置存储"), this);

    QFont titleFont("WenQuanYi Micro Hei", 30, QFont::Bold);
    QFont bodyFont("WenQuanYi Micro Hei", 22);
    title->setFont(titleFont);
    body->setFont(bodyFont);
    body->setWordWrap(true);

    root->addWidget(title, 0);
    root->addWidget(body, 1);
}
