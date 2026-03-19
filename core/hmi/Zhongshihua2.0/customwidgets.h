#ifndef TCU_HMI_CUSTOMWIDGETS_H
#define TCU_HMI_CUSTOMWIDGETS_H

#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QTimer>
#include <QWidget>

class CClickLabel : public QLabel
{
    Q_OBJECT

public:
    explicit CClickLabel(QWidget *parent = 0);

signals:
    void click();

protected:
    void mouseReleaseEvent(QMouseEvent *event);
};

class BaseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BaseDialog(QWidget *parent = 0);

protected:
    void showEvent(QShowEvent *event);

private slots:
    void refreshHeader();

private:
    QTimer m_timer;
};

class WNumEdit : public QLineEdit
{
    Q_OBJECT
    Q_ENUMS(EDITTYPE)
    Q_PROPERTY(EDITTYPE edttype READ edttype WRITE setEdtType)

public:
    enum EDITTYPE {
        PASSWORD,
        CARDNUMBER,
        MONEY,
        MOBILEPHONE,
        PINCODE,
        TIPPRICE,
        NORMALPRICE,
        HIGHPRICE,
        LOWPRICE,
        RECORDNUM,
        CODESN,
        MONTH,
        SERVERIP,
        DDB,
        DEVID,
        XISHU,
        ZHAN
    };

    explicit WNumEdit(QWidget *parent = 0);

    void setEdtType(EDITTYPE edttype);
    EDITTYPE edttype() const;

protected:
    void mousePressEvent(QMouseEvent *event);

private:
    EDITTYPE m_type;
};

class QRWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QRWidget(QWidget *parent = 0);
    void setQRData(const QString &data);
    void setQRData1(const QString &data1);

protected:
    void paintEvent(QPaintEvent *event);

private:
    QString m_data;
    QString m_data1;
};

class CCellWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CCellWidget(QWidget *parent = 0);
    void setPort(int port);
    void setTips(const QString &msg);
    void setChargeState(float kwh, float money, int minute, float volt, float current, int soc);

signals:
    void cellclick(int port);

private slots:
    void emitClick();

protected:
    void resizeEvent(QResizeEvent *event);

private:
    int m_port;
    QFrame *m_frame;
    CClickLabel *m_lblKwh;
    CClickLabel *m_lblMoney;
    CClickLabel *m_lblTime;
    CClickLabel *m_lblVA;
    CClickLabel *m_lblSoc;
    CClickLabel *m_lblHint;
    QProgressBar *m_progress;
};

#endif
