#include "state_page.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QPalette>

StatePage::StatePage(QWidget* parent)
    : QWidget(parent)
    , m_title(new QLabel(this))
    , m_message(new QLabel(this))
    , m_metrics(new QLabel(this))
    , m_footer(new QLabel(this))
{
    // BY ZF: 状态页采用统一布局，便于后续替换成正式视觉稿。
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 48, 24, 24);
    root->setSpacing(18);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(15, 24, 35));
    pal.setColor(QPalette::WindowText, QColor(238, 244, 250));
    setAutoFillBackground(true);
    setPalette(pal);

    QFont titleFont("WenQuanYi Micro Hei", 32, QFont::Bold);
    QFont msgFont("WenQuanYi Micro Hei", 24, QFont::Bold);
    QFont metricsFont("WenQuanYi Micro Hei", 22);
    QFont footerFont("WenQuanYi Micro Hei", 18);

    m_title->setFont(titleFont);
    m_message->setFont(msgFont);
    m_metrics->setFont(metricsFont);
    m_footer->setFont(footerFont);

    m_title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_message->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_metrics->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_footer->setAlignment(Qt::AlignLeft | Qt::AlignBottom);

    m_message->setWordWrap(true);
    m_metrics->setWordWrap(true);
    m_footer->setWordWrap(true);

    root->addWidget(m_title, 0);
    root->addWidget(m_message, 0);
    root->addWidget(m_metrics, 1);
    root->addWidget(m_footer, 0);
}

void StatePage::setTitle(const QString& text)
{
    m_title->setText(text);
}

void StatePage::setMessage(const QString& text)
{
    m_message->setText(text);
}

void StatePage::setMetrics(const QString& text)
{
    m_metrics->setText(text);
}

void StatePage::setFooter(const QString& text)
{
    m_footer->setText(text);
}
