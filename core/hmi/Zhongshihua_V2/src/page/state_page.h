#ifndef ZSHV2_STATE_PAGE_H
#define ZSHV2_STATE_PAGE_H

#include <QWidget>

class QLabel;

class StatePage : public QWidget {
    Q_OBJECT

public:
    explicit StatePage(QWidget* parent = 0);

    void setTitle(const QString& text);
    void setMessage(const QString& text);
    void setMetrics(const QString& text);
    void setFooter(const QString& text);

private:
    QLabel* m_title;
    QLabel* m_message;
    QLabel* m_metrics;
    QLabel* m_footer;
};

#endif // ZSHV2_STATE_PAGE_H
