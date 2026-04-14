#include "uipages.h"

QWidget *createPreviewPage(const QString &path, QWidget *parent)
{
    if (path == ":/ui/a3about.ui") return new A3AboutPage(parent);
    if (path == ":/ui/b1idle.ui") return new B1IdlePage(parent);
    if (path == ":/ui/c6flushcad.ui") return new C6FlushCadPage(parent);
    if (path == ":/ui/e1chargeinfo.ui") return new E1ChargeInfoPage(parent);
    if (path == ":/ui/s2stopping.ui") return new S2StoppingPage(parent);
    if (path == ":/ui/f7checkoutok.ui") return new F7CheckoutOkPage(parent);
    if (path == ":/ui/numinputdlg.ui") return new NumInputDlgPage(parent);
    if (path == ":/ui/numinputfdlg.ui") return new NumInputFDlgPage(parent);
    return 0;
}
