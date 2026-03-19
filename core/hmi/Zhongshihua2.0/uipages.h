#ifndef TCU_HMI_UIPAGES_H
#define TCU_HMI_UIPAGES_H

#include <QDialog>
#include <QWidget>

#include "customwidgets.h"

#include "ui_a3about.h"
#include "ui_b1idle.h"
#include "ui_c6flushcad.h"
#include "ui_e1chargeinfo.h"
#include "ui_f7checkoutok.h"
#include "ui_numinputdlg.h"
#include "ui_numinputfdlg.h"

class A3AboutPage : public QWidget
{
    Q_OBJECT
public:
    explicit A3AboutPage(QWidget *parent = 0) : QWidget(parent) { ui.setupUi(this); }
private:
    Ui::A3AboutClass ui;
};

class B1IdlePage : public BaseDialog
{
    Q_OBJECT
public:
    explicit B1IdlePage(QWidget *parent = 0) : BaseDialog(parent) { ui.setupUi(this); }
private:
    Ui::B1IdleClass ui;
};

class C6FlushCadPage : public BaseDialog
{
    Q_OBJECT
public:
    explicit C6FlushCadPage(QWidget *parent = 0) : BaseDialog(parent) { ui.setupUi(this); }
private:
    Ui::C6FlushCadClass ui;
};

class E1ChargeInfoPage : public BaseDialog
{
    Q_OBJECT
public:
    explicit E1ChargeInfoPage(QWidget *parent = 0) : BaseDialog(parent) { ui.setupUi(this); }
private:
    Ui::E1ChargeInfoClass ui;
};

class F7CheckoutOkPage : public BaseDialog
{
    Q_OBJECT
public:
    explicit F7CheckoutOkPage(QWidget *parent = 0) : BaseDialog(parent) { ui.setupUi(this); }
private:
    Ui::F7CheckoutOkClass ui;
};

class NumInputDlgPage : public QDialog
{
    Q_OBJECT
public:
    explicit NumInputDlgPage(QWidget *parent = 0) : QDialog(parent) { ui.setupUi(this); }
private:
    Ui::NumInputDlgClass ui;
};

class NumInputFDlgPage : public QDialog
{
    Q_OBJECT
public:
    explicit NumInputFDlgPage(QWidget *parent = 0) : QDialog(parent) { ui.setupUi(this); }
private:
    Ui::NumInputFDlgClass ui;
};

QWidget *createPreviewPage(const QString &path, QWidget *parent);

#endif
