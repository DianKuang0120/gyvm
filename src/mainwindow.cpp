#include "mainwindow.h"
#include "store.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <unistd.h>

namespace {

// 把秒数格式化成 "1h 23m" / "4m 05s" / "12s"，状态栏空间很小
QString formatDuration(int sec)
{
    if (sec < 60)
        return QStringLiteral("%1s").arg(sec);
    if (sec < 3600)
        return QStringLiteral("%1m %2s").arg(sec / 60).arg(sec % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1h %2m").arg(sec / 3600).arg((sec % 3600) / 60);
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("gyvm — Gunyah 虚拟机管理"));

    // 按屏幕自适应大小（手机竖屏很窄，之前固定的 1080 宽放不下）
    if (const QScreen *s = QGuiApplication::primaryScreen()) {
        const QRect g = s->availableGeometry();
        resize(qBound(420, int(g.width() * 0.97), 1100),
               qBound(520, int(g.height() * 0.92), 1500));
        move(g.topLeft());
    } else {
        resize(700, 1200);
    }

    buildUi();
    buildActions();
    loadSettings();
    updateGpuAvailability();
    checkShmem();

    connect(&m_backend, &Backend::output, this, &MainWindow::onOutput);
    connect(&m_backend, &Backend::stateChanged, this, &MainWindow::onStateChanged);

    reloadList();
}

// ---------------------------------------------------------------- 界面

void MainWindow::buildUi()
{
    // 左侧：虚拟机列表
    m_list = new QListWidget(this);
    m_list->setMinimumWidth(140);
    m_list->setMaximumWidth(300);
    connect(m_list, &QListWidget::currentRowChanged, this, &MainWindow::onSelectionChanged);

    // 右侧：配置 / 控制台
    m_name = new QLineEdit(this);
    m_kernel = new QLineEdit(this);
    m_initrd = new QLineEdit(this);
    m_mem = new QSpinBox(this);
    m_mem->setRange(128, 16384);
    m_mem->setSuffix(QStringLiteral(" MiB"));
    m_cpus = new QSpinBox(this);
    m_cpus->setRange(1, 32);
    m_protected = new QCheckBox(QStringLiteral("受保护 VM（--protected-vm-without-firmware）"), this);
    m_huge = new QCheckBox(QStringLiteral("大页（--hugepages）"), this);
    m_gpu = new QCheckBox(QStringLiteral("图形窗口（virtio-gpu）"), this);
    m_net = new QCheckBox(QStringLiteral("网络（virtio-net / tap）"), this);
    m_net->setToolTip(QStringLiteral(
        "⚠ 实验性：本机 virtio-net + tap 曾导致 crosvm 卡死（D 状态、不可杀）。\n"
        "只有在明确需要且已建好 tap 设备时才勾选。"));
    m_tap = new QLineEdit(this);
    m_tap->setPlaceholderText(QStringLiteral("gyvm0"));

    auto *kernelRow = new QWidget(this);
    auto *kl = new QHBoxLayout(kernelRow);
    kl->setContentsMargins(0, 0, 0, 0);
    kl->addWidget(m_kernel, 1);
    auto *bk = new QPushButton(QStringLiteral("浏览…"), this);
    connect(bk, &QPushButton::clicked, this, &MainWindow::onBrowseKernel);
    kl->addWidget(bk);

    auto *initrdRow = new QWidget(this);
    auto *il = new QHBoxLayout(initrdRow);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(m_initrd, 1);
    auto *bi = new QPushButton(QStringLiteral("浏览…"), this);
    connect(bi, &QPushButton::clicked, this, &MainWindow::onBrowseInitrd);
    il->addWidget(bi);

    auto *memRow = new QWidget(this);
    auto *ml = new QHBoxLayout(memRow);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->addWidget(m_mem, 1);
    m_memHint = new QLabel(this);
    m_memHint->setStyleSheet(QStringLiteral("color:#8ab4f8;"));
    ml->addWidget(m_memHint);
    connect(m_mem, &QSpinBox::valueChanged, this, [this](int) { updateMemHint(); });
    updateMemHint();

    auto *cmdlineRow = new QWidget(this);
    auto *cl = new QHBoxLayout(cmdlineRow);
    cl->setContentsMargins(0, 0, 0, 0);
    m_rootDevice = new QLineEdit(this);
    m_rootDevice->setPlaceholderText(QStringLiteral("UUID=xxxx 或 /dev/vda1（留空则不加 root=）"));
    m_extraCmdline = new QLineEdit(this);
    m_extraCmdline->setPlaceholderText(QStringLiteral("例如 systemd.mask=systemd-networkd-wait-online.service"));
    cl->addWidget(new QLabel(QStringLiteral("root="), this));
    cl->addWidget(m_rootDevice, 2);
    cl->addSpacing(8);
    cl->addWidget(new QLabel(QStringLiteral("附加"), this));
    cl->addWidget(m_extraCmdline, 3);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("名称"), m_name);
    form->addRow(QStringLiteral("内核 Image"), kernelRow);
    form->addRow(QStringLiteral("initramfs"), initrdRow);
    form->addRow(QStringLiteral("内核命令行"), cmdlineRow);
    form->addRow(QStringLiteral("内存"), memRow);
    form->addRow(QStringLiteral("CPU"), m_cpus);

    // 磁盘
    m_disks = new QListWidget(this);
    m_disks->setMaximumHeight(110);
    auto *diskBtns = new QWidget(this);
    auto *dl = new QVBoxLayout(diskBtns);
    dl->setContentsMargins(0, 0, 0, 0);
    auto *r1 = new QHBoxLayout;
    auto *r2 = new QHBoxLayout;
    auto *bdCreate = new QPushButton(QStringLiteral("创建镜像"), this);
    auto *bdAttach = new QPushButton(QStringLiteral("挂载"), this);
    auto *bdIso = new QPushButton(QStringLiteral("挂载 ISO"), this);
    auto *bdRemove = new QPushButton(QStringLiteral("移除"), this);
    auto *bdResize = new QPushButton(QStringLiteral("扩容"), this);
    auto *bdClone = new QPushButton(QStringLiteral("克隆"), this);
    connect(bdCreate, &QPushButton::clicked, this, &MainWindow::onCreateDisk);
    connect(bdAttach, &QPushButton::clicked, this, &MainWindow::onAttachDisk);
    connect(bdIso, &QPushButton::clicked, this, &MainWindow::onAttachIso);
    connect(bdRemove, &QPushButton::clicked, this, &MainWindow::onRemoveDisk);
    connect(bdResize, &QPushButton::clicked, this, &MainWindow::onDiskResize);
    connect(bdClone, &QPushButton::clicked, this, &MainWindow::onDiskClone);
    r1->addWidget(bdCreate);
    r1->addWidget(bdAttach);
    r1->addWidget(bdIso);
    r1->addWidget(bdRemove);
    r1->addStretch(1);
    r2->addWidget(bdResize);
    r2->addWidget(bdClone);
    r2->addStretch(1);
    dl->addLayout(r1);
    dl->addLayout(r2);

    auto *devices = new QGroupBox(QStringLiteral("设备"), this);
    auto *dv = new QVBoxLayout(devices);
    dv->addWidget(m_protected);
    dv->addWidget(m_huge);
    dv->addWidget(m_gpu);
    auto *netRow = new QWidget(this);
    auto *nl = new QHBoxLayout(netRow);
    nl->setContentsMargins(0, 0, 0, 0);
    nl->addWidget(m_net);
    nl->addWidget(new QLabel(QStringLiteral("tap:"), this));
    nl->addWidget(m_tap, 1);
    dv->addWidget(netRow);
    dv->addWidget(new QLabel(QStringLiteral("磁盘（virtio-blk）："), this));
    dv->addWidget(m_disks);
    dv->addWidget(diskBtns);

    dv->addWidget(new QLabel(QStringLiteral("共享目录（virtio-fs）："), this));
    m_sharedDirs = new QListWidget(this);
    m_sharedDirs->setMaximumHeight(90);
    dv->addWidget(m_sharedDirs);
    auto *shareBtns = new QWidget(this);
    auto *sl = new QHBoxLayout(shareBtns);
    sl->setContentsMargins(0, 0, 0, 0);
    auto *bsAdd = new QPushButton(QStringLiteral("添加目录"), this);
    auto *bsDel = new QPushButton(QStringLiteral("移除"), this);
    connect(bsAdd, &QPushButton::clicked, this, &MainWindow::onAddSharedDir);
    connect(bsDel, &QPushButton::clicked, this, &MainWindow::onRemoveSharedDir);
    sl->addWidget(bsAdd);
    sl->addWidget(bsDel);
    sl->addStretch(1);
    dv->addWidget(shareBtns);

    // UEFI 变量存储（UEFI 启动必需）+ pstore
    dv->addWidget(new QLabel(QStringLiteral("UEFI 变量（pflash）："), this));
    auto *pfRow = new QWidget(this);
    auto *pfl = new QHBoxLayout(pfRow);
    pfl->setContentsMargins(0, 0, 0, 0);
    m_pflash = new QLineEdit(this);
    m_pflash->setPlaceholderText(QStringLiteral("变量文件 .fd（UEFI 启动用，可新建）"));
    auto *pfBrowse = new QPushButton(QStringLiteral("选择/新建"), this);
    connect(pfBrowse, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getSaveFileName(
            this, QStringLiteral("UEFI 变量文件"), QDir::homePath(),
            QStringLiteral("固件 (*.fd);;所有文件 (*)"));
        if (!p.isEmpty())
            m_pflash->setText(p);
    });
    pfl->addWidget(m_pflash, 1);
    pfl->addWidget(pfBrowse);
    dv->addWidget(pfRow);

    m_pstore = new QCheckBox(QStringLiteral("保留客户机内核日志（--pstore）"), this);
    dv->addWidget(m_pstore);

    // 高级：直接追加 crosvm 参数（一行一个），用来试新开关或本程序还没覆盖的选项
    auto *adv = new QGroupBox(QStringLiteral("高级：追加 crosvm 参数"), this);
    adv->setCheckable(true);
    adv->setChecked(false); // 默认收起，避免占地方
    auto *av = new QVBoxLayout(adv);
    m_extraArgs = new QPlainTextEdit(this);
    m_extraArgs->setPlaceholderText(
        QStringLiteral("一行一个，原样追加到 crosvm 命令行末尾（内核路径之前）\n"
                       "例如：\n"
                       "  --vsock cid=3\n"
                       "  --input keyboard\n"
                       "  --async-executor uring"));
    m_extraArgs->setMaximumHeight(90);
    m_extraArgs->setStyleSheet(QStringLiteral("font-family: monospace;"));
    av->addWidget(m_extraArgs);
    dv->addWidget(adv);

    auto *configPage = new QWidget(this);
    auto *cv = new QVBoxLayout(configPage);
    cv->addLayout(form);
    cv->addWidget(devices);
    cv->addStretch(1);

    // 竖屏放不下 -> 配置页放进滚动区（内容高时能上下滚）
    auto *configScroll = new QScrollArea(this);
    configScroll->setWidgetResizable(true);
    configScroll->setFrameShape(QFrame::NoFrame);
    configScroll->setWidget(configPage);

    // 控制台
    m_console = new QPlainTextEdit(this);
    m_console->setReadOnly(true);
    m_console->setStyleSheet(QStringLiteral("font-family: monospace;"));
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("输入并回车发送到客户机控制台…"));
    connect(m_input, &QLineEdit::returnPressed, this, &MainWindow::onSendInput);
    auto *consolePage = new QWidget(this);
    auto *pv = new QVBoxLayout(consolePage);
    pv->addWidget(m_console, 1);
    pv->addWidget(m_input);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(configScroll, QStringLiteral("配置"));
    tabs->addTab(consolePage, QStringLiteral("控制台"));

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(m_list);
    split->addWidget(tabs);
    split->setStretchFactor(1, 1);
    setCentralWidget(split);

    m_status = new QLabel(this);
    statusBar()->addWidget(m_status, 1);
    // 运行时资源占用（只在选中的 VM 运行时刷新）
    m_statsLabel = new QLabel(this);
    m_statsLabel->setStyleSheet(QStringLiteral("color:#8ab4f8;"));
    statusBar()->addPermanentWidget(m_statsLabel);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1500);
    connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::onRefreshStats);
    m_statsTimer->start();
}

void MainWindow::buildActions()
{
    auto *mFile = menuBar()->addMenu(QStringLiteral("文件"));
    auto *mNew = mFile->addAction(QIcon::fromTheme(QStringLiteral("document-new")),
                                  QStringLiteral("新建虚拟机"));
    connect(mNew, &QAction::triggered, this, &MainWindow::onNewVm);
    auto *mTpl = mFile->addAction(QIcon::fromTheme(QStringLiteral("document-new-from-template")),
                                  QStringLiteral("从模板新建…"));
    mTpl->setToolTip(QStringLiteral("用 ~/Gunyah/images 里准备好的镜像快速建一台"));
    connect(mTpl, &QAction::triggered, this, &MainWindow::onNewFromTemplate);
    auto *mSave = mFile->addAction(QIcon::fromTheme(QStringLiteral("document-save")),
                                   QStringLiteral("保存配置"));
    connect(mSave, &QAction::triggered, this, &MainWindow::onSaveVm);
    auto *mClone = mFile->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
                                    QStringLiteral("克隆虚拟机…"));
    connect(mClone, &QAction::triggered, this, &MainWindow::onCloneVm);
    mFile->addSeparator();
    auto *mDel = mFile->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                  QStringLiteral("删除虚拟机"));
    connect(mDel, &QAction::triggered, this, &MainWindow::onDeleteVm);
    mFile->addSeparator();
    auto *mQuit = mFile->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                   QStringLiteral("退出"));
    connect(mQuit, &QAction::triggered, qApp, &QApplication::quit);

    auto *mDev = menuBar()->addMenu(QStringLiteral("虚拟机"));
    m_actStart = mDev->addAction(QIcon::fromTheme(QStringLiteral("media-playback-start")),
                                 QStringLiteral("启动"));
    connect(m_actStart, &QAction::triggered, this, &MainWindow::onStart);
    m_actStop = mDev->addAction(QIcon::fromTheme(QStringLiteral("media-playback-stop")),
                                QStringLiteral("停止"));
    connect(m_actStop, &QAction::triggered, this, &MainWindow::onStop);
    m_actRestart = mDev->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                   QStringLiteral("重启"));
    m_actRestart->setToolTip(QStringLiteral("停止后立即重新启动（配置改动会生效）"));
    connect(m_actRestart, &QAction::triggered, this, &MainWindow::onRestart);

    auto *mSet = menuBar()->addMenu(QStringLiteral("设置"));
    auto *mCmd = mSet->addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")),
                                 QStringLiteral("查看将要执行的命令"));
    connect(mCmd, &QAction::triggered, this, &MainWindow::onShowCommand);
    auto *mLog = mSet->addAction(QIcon::fromTheme(QStringLiteral("text-x-generic")),
                                 QStringLiteral("打开控制台日志"));
    connect(mLog, &QAction::triggered, this, &MainWindow::onOpenLog);
    auto *mGh = mSet->addAction(QIcon::fromTheme(QStringLiteral("view-list-details")),
                                QStringLiteral("Gunyah 驱动状态…"));
    mGh->setToolTip(QStringLiteral("读取 /sys/kernel/debug/gunyah，查看 VM 内存、资源、RPC 延迟"));
    connect(mGh, &QAction::triggered, this, &MainWindow::onGunyahInfo);
    auto *mCheck = mSet->addAction(QIcon::fromTheme(QStringLiteral("dialog-information")),
                                   QStringLiteral("环境自检"));
    connect(mCheck, &QAction::triggered, this, &MainWindow::onSelfCheck);
    mSet->addSeparator();
    auto *mPref = mSet->addAction(QIcon::fromTheme(QStringLiteral("configure")),
                                  QStringLiteral("首选项"));
    connect(mPref, &QAction::triggered, this, &MainWindow::onPreferences);

    auto *tb = addToolBar(QStringLiteral("主工具栏"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->addAction(mNew);
    tb->addAction(m_actStart);
    tb->addAction(m_actStop);
    tb->addAction(m_actRestart);
    tb->addSeparator();
    tb->addAction(mSave);
    tb->addAction(mDel);
}

// ---------------------------------------------------------------- 设置

void MainWindow::loadSettings()
{
    QSettings s;
    QString crosvm = s.value(QStringLiteral("crosvm/path")).toString();
    if (crosvm.isEmpty())
        crosvm = QFile::exists(QStringLiteral("/opt/crosvm/crosvm"))
                     ? QStringLiteral("/opt/crosvm/crosvm")
                     : QStringLiteral("crosvm");
    m_backend.setCrosvmPath(crosvm);

    QStringList prefix;
    for (const QString &p : s.value(QStringLiteral("crosvm/prefix"),
                                    QStringList{QStringLiteral("sudo"), QStringLiteral("-A")})
                               .toStringList())
        if (!p.isEmpty())
            prefix << p;
    m_backend.setLaunchPrefix(prefix);
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("crosvm/path"), m_backend.crosvmPath());
    s.setValue(QStringLiteral("crosvm/prefix"), m_backend.launchPrefix());
}

void MainWindow::onPreferences()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("首选项"));
    auto *f = new QFormLayout(&d);
    auto *path = new QLineEdit(m_backend.crosvmPath(), &d);
    auto *prefix = new QLineEdit(m_backend.launchPrefix().join(QLatin1Char(' ')), &d);
    prefix->setPlaceholderText(QStringLiteral("例如 sudo / pkexec（留空表示直接运行）"));
    f->addRow(QStringLiteral("crosvm 路径"), path);
    f->addRow(QStringLiteral("提权前缀"), prefix);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() == QDialog::Accepted) {
        m_backend.setCrosvmPath(path->text().trimmed());
        const QStringList parts =
            prefix->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        m_backend.setLaunchPrefix(parts);
        saveSettings();
        updateGpuAvailability();
        note(QStringLiteral("已保存设置"));
    }
}

// ---------------------------------------------------------------- 列表/表单

void MainWindow::reloadList(const QString &select)
{
    const QString keep = select.isEmpty() ? selectedName() : select;
    m_list->clear();
    for (const QString &n : Store::listNames()) {
        const bool run = m_backend.running(n);
        auto *it = new QListWidgetItem(
            QIcon::fromTheme(run ? QStringLiteral("media-playback-start")
                                 : QStringLiteral("media-playback-pause")),
            n, m_list);
        it->setData(Qt::UserRole, n);
        it->setToolTip(run ? QStringLiteral("运行中") : QStringLiteral("已停止"));
    }
    if (!keep.isEmpty()) {
        const auto items = m_list->findItems(keep, Qt::MatchStartsWith);
        if (!items.isEmpty())
            m_list->setCurrentItem(items.first());
    }
    if (m_list->currentRow() < 0 && m_list->count() > 0)
        m_list->setCurrentRow(0);
    onSelectionChanged();
}

QString MainWindow::selectedName() const
{
    auto *it = m_list ? m_list->currentItem() : nullptr;
    return it ? it->data(Qt::UserRole).toString() : QString();
}

void MainWindow::loadForm(const VmConfig &vm)
{
    m_name->setText(vm.name);
    m_name->setReadOnly(true);
    m_kernel->setText(vm.kernel);
    m_initrd->setText(vm.initrd);
    m_mem->setValue(vm.memoryMiB);
    m_cpus->setValue(vm.cpus);
    m_protected->setChecked(vm.protectedVm);
    m_huge->setChecked(vm.hugepages);
    m_gpu->setChecked(vm.gpu);
    m_net->setChecked(vm.net);
    m_tap->setText(vm.tapName);
    m_disks->clear();
    for (const DiskAttach &d : vm.disks) {
        QString label = d.path;
        if (d.cdrom)
            label += QStringLiteral("  [ISO]");
        else if (d.readOnly)
            label += QStringLiteral("  [只读]");
        auto *it = new QListWidgetItem(label, m_disks);
        it->setData(Qt::UserRole, d.readOnly);
        it->setData(Qt::UserRole + 1, d.cdrom);
        it->setData(Qt::UserRole + 2, d.path);
    }
    if (m_rootDevice)
        m_rootDevice->setText(vm.rootDevice);
    if (m_extraCmdline)
        m_extraCmdline->setText(vm.extraCmdline);
    if (m_extraArgs)
        m_extraArgs->setPlainText(vm.extraArgs.join(QLatin1Char('\n')));
    if (m_pflash)
        m_pflash->setText(vm.pflash);
    if (m_pstore)
        m_pstore->setChecked(vm.pstore);
    if (m_sharedDirs) {
        m_sharedDirs->clear();
        for (const SharedDir &s : vm.sharedDirs) {
            auto *it = new QListWidgetItem(
                s.path + QStringLiteral("  →  ") + s.tag + (s.readOnly ? QStringLiteral("  [只读]") : QString()),
                m_sharedDirs);
            it->setData(Qt::UserRole, s.path);
            it->setData(Qt::UserRole + 1, s.tag);
            it->setData(Qt::UserRole + 2, s.readOnly);
        }
    }
}

VmConfig MainWindow::formConfig(bool forSave) const
{
    VmConfig vm;
    vm.name = m_name->text().trimmed();
    vm.kernel = m_kernel->text().trimmed();
    vm.initrd = m_initrd->text().trimmed();
    vm.memoryMiB = m_mem->value();
    vm.cpus = m_cpus->value();
    vm.protectedVm = m_protected->isChecked();
    vm.hugepages = m_huge->isChecked();
    vm.gpu = m_gpu->isChecked();
    vm.net = m_net->isChecked();
    vm.tapName = m_tap->text().trimmed();
    if (m_extraArgs) {
        const QStringList lines =
            m_extraArgs->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &l : lines)
            if (!l.trimmed().isEmpty())
                vm.extraArgs << l.trimmed();
    }
    vm.rootDevice = m_rootDevice ? m_rootDevice->text().trimmed() : QString();
    vm.extraCmdline = m_extraCmdline ? m_extraCmdline->text().trimmed() : QString();
    vm.pflash = m_pflash ? m_pflash->text().trimmed() : QString();
    vm.pstore = m_pstore && m_pstore->isChecked();
    for (int i = 0; i < m_disks->count(); ++i) {
        QListWidgetItem *it = m_disks->item(i);
        DiskAttach d;
        d.path = it->data(Qt::UserRole + 2).toString();
        d.readOnly = it->data(Qt::UserRole).toBool();
        d.cdrom = it->data(Qt::UserRole + 1).toBool();
        if (!d.path.isEmpty())
            vm.disks.append(d);
    }
    if (m_sharedDirs) {
        for (int i = 0; i < m_sharedDirs->count(); ++i) {
            QListWidgetItem *it = m_sharedDirs->item(i);
            SharedDir s;
            s.path = it->data(Qt::UserRole).toString();
            s.tag = it->data(Qt::UserRole + 1).toString();
            s.readOnly = it->data(Qt::UserRole + 2).toBool();
            vm.sharedDirs.append(s);
        }
    }
    return vm;
}

void MainWindow::onSelectionChanged()
{
    const QString n = selectedName();
    const bool has = !n.isEmpty();
    m_console->clear();
    if (has) {
        loadForm(Store::load(n));
        setRunningUi(m_backend.running(n));
    } else {
        loadForm(VmConfig{});
    }
    note(has ? QStringLiteral("已选择：%1").arg(n) : QStringLiteral("没有虚拟机，点“新建虚拟机…”"));
}

// ---------------------------------------------------------------- 动作

void MainWindow::onNewFromTemplate()
{
    // 模板就放在 ~/Gunyah/images/<name>/ 下：Image + initrd.img + 系统盘
    const QString tplRoot = QDir::homePath() + QStringLiteral("/Gunyah/images");
    const QStringList tpls =
        QDir(tplRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)
            .filter(QRegularExpression(QStringLiteral("^ubuntu|^debian|^alpine")));
    if (tpls.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("没有模板"),
            QStringLiteral("在 %1 下没有找到模板目录。\n\n"
                           "一个模板需要包含：\n"
                           "  Image        未压缩的 arm64 内核\n"
                           "  initrd.img   对应的 initramfs\n"
                           "  rootfs.img / rootfs.qcow2 系统盘（可选）")
                .arg(tplRoot));
        return;
    }

    bool ok = false;
    const QString tpl = QInputDialog::getItem(this, QStringLiteral("从模板新建"),
                                              QStringLiteral("模板："), tpls, 0, false, &ok);
    if (!ok || tpl.isEmpty())
        return;

    const QString dir = tplRoot + QLatin1Char('/') + tpl;
    const QString name = QInputDialog::getText(this, QStringLiteral("虚拟机名称"),
                                               QStringLiteral("名称："), QLineEdit::Normal,
                                               tpl, &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (Store::exists(name)) {
        note(QStringLiteral("「%1」已存在").arg(name), true);
        return;
    }

    VmConfig vm;
    vm.name = name;
    vm.kernel = dir + QStringLiteral("/Image");
    vm.initrd = dir + QStringLiteral("/initrd.img");
    vm.memoryMiB = 2048;
    vm.cpus = 2;
    vm.protectedVm = true;
    vm.hugepages = true;
    // Ubuntu cloud image 的根分区 UUID，外加没有网卡时跳过 networkd 等待
    if (tpl.startsWith(QLatin1String("ubuntu"))) {
        vm.rootDevice = QStringLiteral("UUID=da3ec87b-3550-4f39-ab67-09f9e985ca0d");
        vm.extraCmdline = QStringLiteral("rw systemd.mask=systemd-networkd-wait-online.service");
    }

    // 系统盘必须复制一份，否则所有模板实例会共写同一个镜像
    // 优先 raw：实测这个 crosvm 的 qcow2 后端读 cloud image 会报
    // "Buffer I/O error on dev vda"，raw 正常。
    QString tplDisk = dir + QStringLiteral("/rootfs.img");
    if (!QFile::exists(tplDisk))
        tplDisk = dir + QStringLiteral("/rootfs.qcow2");
    if (QFile::exists(tplDisk)) {
        QDir().mkpath(Store::vmDir(name));
        const QString newDisk =
            Store::vmDir(name) + QLatin1Char('/') + QFileInfo(tplDisk).fileName();
        if (!QFile::copy(tplDisk, newDisk)) {
            note(QStringLiteral("复制系统盘失败：%1").arg(newDisk), true);
            return;
        }
        DiskAttach d;
        d.path = newDisk;
        vm.disks.append(d);
    }

    if (!Store::save(vm)) {
        note(QStringLiteral("保存配置失败"), true);
        return;
    }
    reloadList(name);
    note(QStringLiteral("已从模板「%1」创建「%2」").arg(tpl, name));
}

void MainWindow::onNewVm()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("新建虚拟机"));
    auto *f = new QFormLayout(&d);

    auto *name = new QLineEdit(&d);
    auto *kernel = new QLineEdit(&d);
    auto *initrd = new QLineEdit(&d);
    auto *mem = new QSpinBox(&d);
    mem->setRange(128, 16384);
    mem->setValue(1024);
    mem->setSuffix(QStringLiteral(" MiB"));
    auto *cpus = new QSpinBox(&d);
    cpus->setRange(1, 32);
    cpus->setValue(2);

    auto *kRow = new QWidget(&d);
    auto *kl = new QHBoxLayout(kRow);
    kl->setContentsMargins(0, 0, 0, 0);
    kl->addWidget(kernel, 1);
    auto *kb = new QPushButton(QStringLiteral("浏览…"), &d);
    kl->addWidget(kb);
    connect(kb, &QPushButton::clicked, &d, [&]() {
        const QString p = QFileDialog::getOpenFileName(&d, QStringLiteral("选择内核 Image"),
                                                       QStringLiteral("/home"), QString());
        if (!p.isEmpty())
            kernel->setText(p);
    });

    auto *iRow = new QWidget(&d);
    auto *il = new QHBoxLayout(iRow);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(initrd, 1);
    auto *ib = new QPushButton(QStringLiteral("浏览…"), &d);
    il->addWidget(ib);
    connect(ib, &QPushButton::clicked, &d, [&]() {
        const QString p = QFileDialog::getOpenFileName(&d, QStringLiteral("选择 initramfs"),
                                                       QStringLiteral("/home"), QString());
        if (!p.isEmpty())
            initrd->setText(p);
    });

    f->addRow(QStringLiteral("名称"), name);
    f->addRow(QStringLiteral("内核 Image"), kRow);
    f->addRow(QStringLiteral("initramfs"), iRow);
    f->addRow(QStringLiteral("内存"), mem);
    f->addRow(QStringLiteral("CPU"), cpus);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);

    if (d.exec() != QDialog::Accepted)
        return;

    VmConfig vm;
    vm.name = name->text().trimmed();
    vm.kernel = kernel->text().trimmed();
    vm.initrd = initrd->text().trimmed();
    vm.memoryMiB = mem->value();
    vm.cpus = cpus->value();
    vm.protectedVm = true;
    vm.hugepages = true;

    if (vm.name.isEmpty()) {
        note(QStringLiteral("名称不能为空"), true);
        return;
    }
    if (Store::exists(vm.name)) {
        note(QStringLiteral("「%1」已存在").arg(vm.name), true);
        return;
    }
    if (!Store::save(vm)) {
        note(QStringLiteral("保存失败"), true);
        return;
    }
    reloadList(vm.name);
    note(QStringLiteral("已创建「%1」").arg(vm.name));
}

void MainWindow::persistForm()
{
    const QString n = selectedName();
    if (n.isEmpty())
        return;
    VmConfig vm = formConfig(true);
    // 名字被改动时不要覆盖别的配置
    if (!vm.name.isEmpty() && vm.name != n)
        return;
    vm.name = n;
    Store::save(vm);
}

void MainWindow::onSaveVm()
{
    const QString n = selectedName();
    if (n.isEmpty())
        return;
    VmConfig vm = formConfig(true);
    if (!Store::save(vm)) {
        note(QStringLiteral("保存失败"), true);
        return;
    }
    reloadList(n);
    note(QStringLiteral("已保存「%1」").arg(n));
}

void MainWindow::onDeleteVm()
{
    const QString n = selectedName();
    if (n.isEmpty())
        return;
    if (m_backend.running(n)) {
        note(QStringLiteral("请先停止「%1」").arg(n), true);
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("删除"),
                              QStringLiteral("确定删除「%1」？磁盘镜像不会删除。").arg(n))
        != QMessageBox::Yes)
        return;
    Store::remove(n);
    reloadList();
    note(QStringLiteral("已删除「%1」").arg(n));
}

void MainWindow::onCloneVm()
{
    const QString src = selectedName();
    if (src.isEmpty())
        return;
    if (m_backend.running(src)) {
        note(QStringLiteral("请先停止「%1」再克隆").arg(src), true);
        return;
    }

    bool ok = false;
    const QString dst = QInputDialog::getText(this, QStringLiteral("克隆虚拟机"),
                                              QStringLiteral("新名称："), QLineEdit::Normal,
                                              src + QStringLiteral("-copy"), &ok)
                            .trimmed();
    if (!ok || dst.isEmpty())
        return;
    if (Store::exists(dst)) {
        note(QStringLiteral("「%1」已存在").arg(dst), true);
        return;
    }
    if (dst.contains(QLatin1Char('/')) || dst.contains(QLatin1Char('\\'))) {
        note(QStringLiteral("名字里不能有路径分隔符"), true);
        return;
    }

    // 磁盘必须复制一份：两个 VM 共写同一个镜像会互相破坏文件系统
    const bool copyDisks =
        QMessageBox::question(this, QStringLiteral("克隆"),
                              QStringLiteral(
                                  "是否一并复制磁盘镜像？\n\n"
                                  "是：完全独立的副本，可同时运行（更慢、占双倍空间）\n"
                                  "否：共用现有镜像，两台不能同时启动"),
                              QMessageBox::Yes | QMessageBox::No)
        == QMessageBox::Yes;

    VmConfig vm = Store::load(src);
    vm.name = dst;
    QDir().mkpath(Store::vmDir(dst));

    QStringList problems;
    if (copyDisks) {
        bool viaQemu = QFile::exists(QStringLiteral("/usr/bin/qemu-img"));
        for (DiskAttach &d : vm.disks) {
            const QFileInfo fi(d.path);
            if (d.cdrom || !fi.exists()) {
                problems << QStringLiteral("%1（ISO 或文件不存在，保持原样）").arg(d.path);
                continue;
            }

            // 新盘放在新 VM 目录下，名字沿用原来的
            const QString newPath = Store::vmDir(dst) + QLatin1Char('/') + fi.fileName();
            bool copied = false;

            if (viaQemu) {
                // qcow2 用 qemu-img 转换，raw 用 cp 更快
                const bool isQcow = d.path.endsWith(QStringLiteral(".qcow2"));
                QProcess q;
                if (isQcow) {
                    q.start(QStringLiteral("qemu-img"),
                            {QStringLiteral("convert"), QStringLiteral("-O"),
                             QStringLiteral("qcow2"), d.path, newPath});
                } else {
                    q.start(QStringLiteral("qemu-img"),
                            {QStringLiteral("convert"), QStringLiteral("-O"),
                             QStringLiteral("raw"), d.path, newPath});
                }
                q.waitForFinished(-1);
                copied = q.exitStatus() == QProcess::NormalExit && q.exitCode() == 0;
            }
            if (!copied)
                copied = QFile::copy(d.path, newPath);

            if (copied)
                d.path = newPath;
            else
                problems << QStringLiteral("%1（复制失败）").arg(fi.fileName());
        }
    }

    // UEFI 变量也要独立，否则两台会互相覆盖启动项
    if (!vm.pflash.isEmpty() && QFile::exists(vm.pflash)) {
        const QString newVars =
            Store::vmDir(dst) + QLatin1Char('/') + QFileInfo(vm.pflash).fileName();
        if (QFile::copy(vm.pflash, newVars))
            vm.pflash = newVars;
        else
            problems << QStringLiteral("UEFI 变量复制失败");
    }

    if (!Store::save(vm)) {
        note(QStringLiteral("保存克隆配置失败"), true);
        return;
    }

    reloadList(dst);
    if (problems.isEmpty()) {
        note(QStringLiteral("已克隆为「%1」").arg(dst));
    } else {
        note(QStringLiteral("已克隆为「%1」，但：%2").arg(dst, problems.join(QStringLiteral("；"))),
             true);
    }
}

void MainWindow::onOpenLog()
{
    const QString n = selectedName();
    if (n.isEmpty())
        return;
    const QString path = Store::consoleLog(n);
    if (!QFile::exists(path)) {
        note(QStringLiteral("「%1」还没有控制台日志").arg(n), true);
        return;
    }
    QProcess::startDetached(QStringLiteral("xdg-open"), {path});
}

namespace {

// 把 /sys/kernel/debug/gunyah 下所有 VM 的状态拼成一段文本
QString readGunyahDebugfs()
{
    static const char *const files[] = {"state", "memory", "resources", "mmio", "rm"};
    const QString root = QStringLiteral("/sys/kernel/debug/gunyah");
    QString out;

    const QStringList vms =
        QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &vmid : vms) {
        out += QStringLiteral("══════ VM %1 ══════\n\n").arg(vmid);
        for (const char *f : files) {
            QFile fh(root + QLatin1Char('/') + vmid + QLatin1Char('/')
                     + QLatin1String(f));
            if (!fh.open(QIODevice::ReadOnly))
                continue;
            out += QStringLiteral("── %1 ──\n").arg(QLatin1String(f));
            out += QString::fromUtf8(fh.readAll());
            out += QLatin1Char('\n');
        }
    }
    return out;
}

} // namespace

void MainWindow::onGunyahInfo()
{
    // 读 /sys/kernel/debug/gunyah/<vmid>/ 下的驱动侧状态。
    // vmid 是 Gunyah 分配的，和 GUI 里的 VM 名字无关，所以全列出来。
    const QString root = QStringLiteral("/sys/kernel/debug/gunyah");
    if (!QDir(root).exists()) {
        QMessageBox::information(
            this, QStringLiteral("Gunyah 驱动状态"),
            QStringLiteral("找不到 %1。\n\n"
                           "可能原因：\n"
                           "  • 驱动编译时没有 CONFIG_DEBUG_FS\n"
                           "  • debugfs 未挂载（sudo mount -t debugfs none /sys/kernel/debug）\n"
                           "  • 当前没有 VM 在运行")
                .arg(root));
        return;
    }

    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Gunyah 驱动状态"));
    d.resize(780, 640);
    auto *lay = new QVBoxLayout(&d);

    auto *view = new QPlainTextEdit(&d);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    const QString initial = readGunyahDebugfs();
    view->setPlainText(initial.isEmpty()
                           ? QStringLiteral("还没有正在运行的 VM。\n启动一台后这里会显示状态。\n")
                           : initial);
    lay->addWidget(view, 1);

    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *refresh = new QPushButton(QStringLiteral("刷新"), &d);
    auto *close = new QPushButton(QStringLiteral("关闭"), &d);
    row->addWidget(refresh);
    row->addWidget(close);
    lay->addLayout(row);

    connect(refresh, &QPushButton::clicked, &d, [view]() {
        const QString t = readGunyahDebugfs();
        view->setPlainText(t.isEmpty() ? QStringLiteral("没有正在运行的 VM。\n") : t);
    });
    connect(close, &QPushButton::clicked, &d, &QDialog::accept);

    d.exec();
}

void MainWindow::onStart()
{
    const QString n = selectedName();
    if (n.isEmpty()) {
        note(QStringLiteral("先选一台虚拟机"), true);
        return;
    }
    VmConfig vm = formConfig(true);

    // 启动前预检：这些条件不满足时 crosvm 只会报一个含糊的 EINVAL，
    // 或者干脆卡死，提前说清楚比事后猜 dmesg 划算。
    QString blocker;
    if (vm.kernel.isEmpty() || !QFile::exists(vm.kernel))
        blocker = QStringLiteral("客户机内核不存在：%1").arg(vm.kernel);
    else if (!vm.initrd.isEmpty() && !QFile::exists(vm.initrd))
        blocker = QStringLiteral("initramfs 不存在：%1").arg(vm.initrd);
    else if (!vm.pflash.isEmpty() && !QFile::exists(vm.pflash))
        blocker = QStringLiteral("UEFI 变量文件不存在：%1").arg(vm.pflash);
    else if (vm.initrd.isEmpty() && vm.disks.isEmpty())
        blocker = QStringLiteral(
            "没有任何启动介质：既没有 initrd，也没有磁盘/ISO，客户机会直接 panic（VFS: Cannot open root device）");

    if (!blocker.isEmpty()) {
        note(blocker, true);
        QMessageBox::warning(this, QStringLiteral("无法启动"), blocker);
        return;
    }

    for (const DiskAttach &d : vm.disks) {
        if (!QFile::exists(d.path)) {
            blocker = QStringLiteral("磁盘镜像不存在：%1").arg(d.path);
            note(blocker, true);
            QMessageBox::warning(this, QStringLiteral("无法启动"), blocker);
            return;
        }
    }

    // 大页检查：客户机内存超过 1 GiB 时 shmem 大页模式不能是 never/deny。
    // advise 就够——crosvm 的 --hugepages 会对自己的 memfd 发 MADV_HUGEPAGE，
    // 大页只体现在 crosvm 的 guest 内存上，不影响系统里其它 tmpfs 使用者。
    if (vm.memoryMiB > 1024) {
        QFile f(QStringLiteral("/sys/kernel/mm/transparent_hugepage/shmem_enabled"));
        if (f.open(QIODevice::ReadOnly)) {
            // sysfs 用方括号标出生效值，形如：
            //   always within_size [advise] never deny force
            // 所以不能拿 startsWith 去比，必须先把方括号里的词取出来。
            const QString mode = QString::fromLatin1(f.read(64)).trimmed();
            const int lb = mode.indexOf(QLatin1Char('['));
            const int rb = mode.indexOf(QLatin1Char(']'));
            QString active;
            if (lb >= 0 && rb > lb)
                active = mode.mid(lb + 1, rb - lb - 1);

            const bool usable = active == QLatin1String("advise") ||
                                active == QLatin1String("always") ||
                                active == QLatin1String("within_size");
            if (!usable) {
                const auto r = QMessageBox::question(
                    this, QStringLiteral("shmem 大页被禁用"),
                    QStringLiteral(
                        "客户机内存 %1 MiB，但 shmem 大页当前生效值是：\n"
                        "  %2\n  （完整内容：%3）\n\n"
                        "never/deny 时无法为大内存客户机建立 memfd，\n"
                        "资源管理器会用 GH_RM_ERROR_NORESOURCE 拒绝 VM_START。\n\n"
                        "改成 advise 吗？（只影响主动请求大页的进程，需要授权）")
                        .arg(vm.memoryMiB)
                        .arg(active.isEmpty() ? QStringLiteral("(无法识别)") : active)
                        .arg(mode));
                if (r == QMessageBox::Yes)
                    m_backend.setShmemThp(QStringLiteral("advise"));
            }
        }
    }

    if (!Store::save(vm)) {
        note(QStringLiteral("保存配置失败"), true);
        return;
    }
    // 网络：先让 daemon 把 tap 和 NAT 准备好，否则 crosvm 会因为
    // tap 不存在或没权限直接失败
    if (vm.net) {
        const QString tap = vm.tapName.isEmpty() ? QStringLiteral("gyvm0") : vm.tapName;
        const QString subnet = QStringLiteral("192.168.77.0/24");
        const QString uplink = m_backend.defaultUplink();
        const QString nerr = m_backend.setupNetwork(tap, subnet, uplink);
        if (!nerr.isEmpty()) {
            note(QStringLiteral("网络配置失败：%1").arg(nerr), true);
            QMessageBox::warning(this, QStringLiteral("网络配置失败"),
                                 QStringLiteral("%1\n\n"
                                                "没有 daemon 时，请先手动建 tap：\n"
                                                "  sudo ip tuntap add dev %2 mode tap\n"
                                                "  sudo ip addr add 192.168.77.1/24 dev %2\n"
                                                "  sudo ip link set %2 up\n"
                                                "  sudo sysctl -w net.ipv4.ip_forward=1\n"
                                                "  sudo iptables -t nat -A POSTROUTING -s 192.168.77.0/24 -o <出口> -j MASQUERADE")
                                     .arg(nerr, tap));
            return;
        }
        note(QStringLiteral("网络就绪：%1 → %2").arg(tap, uplink.isEmpty() ? QStringLiteral("?") : uplink));
    }

    QString err;
    if (!m_backend.start(vm, &err))
        note(QStringLiteral("启动失败：%1").arg(err), true);
    else
        note(QStringLiteral("已启动「%1」").arg(n));
}

void MainWindow::onStop()
{
    const QString n = selectedName();
    if (n.isEmpty())
        return;
    m_backend.stop(n);
    note(QStringLiteral("已停止「%1」").arg(n));
}

void MainWindow::onBrowseKernel()
{
    const QString p = QFileDialog::getOpenFileName(this, QStringLiteral("选择内核 Image"),
                                                   QStringLiteral("/home"), QString());
    if (!p.isEmpty())
        m_kernel->setText(p);
}

void MainWindow::onBrowseInitrd()
{
    const QString p = QFileDialog::getOpenFileName(this, QStringLiteral("选择 initramfs"),
                                                   QStringLiteral("/home"), QString());
    if (!p.isEmpty())
        m_initrd->setText(p);
}

void MainWindow::onCreateDisk()
{
    const QString n = selectedName();
    const QString startDir = n.isEmpty() ? QDir::homePath() : Store::vmDir(n);
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("新建磁盘镜像"), startDir + QStringLiteral("/disk.img"),
        QStringLiteral("raw 镜像 (*.img);;qcow2 镜像 (*.qcow2);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    bool ok = false;
    const int gib = QInputDialog::getInt(this, QStringLiteral("磁盘大小"),
                                         QStringLiteral("大小（GiB）："), 8, 1, 512, 1, &ok);
    if (!ok)
        return;

    const bool qcow2 = path.endsWith(QStringLiteral(".qcow2"), Qt::CaseInsensitive);
    if (qcow2) {
        QProcess q;
        q.start(QStringLiteral("qemu-img"),
                {QStringLiteral("create"), QStringLiteral("-f"), QStringLiteral("qcow2"), path,
                 QStringLiteral("%1G").arg(gib)});
        if (!q.waitForFinished(30000) || q.exitCode() != 0) {
            note(QStringLiteral("qemu-img 创建失败：%1")
                     .arg(QString::fromUtf8(q.readAllStandardError()).trimmed()),
                 true);
            return;
        }
    } else {
        QFile f(path);
        if (!f.open(QIODevice::ReadWrite) || !f.resize(qint64(gib) * 1024 * 1024 * 1024)) {
            note(QStringLiteral("无法创建：%1").arg(path), true);
            return;
        }
        f.close();
    }
    auto *it = new QListWidgetItem(path, m_disks);
    it->setData(Qt::UserRole, false);
    it->setData(Qt::UserRole + 1, false);
    it->setData(Qt::UserRole + 2, path);
    m_disks->setCurrentItem(it);
    persistForm();
    note(QStringLiteral("已创建 %1 GiB 镜像%s：%2")
             .arg(gib)
             .arg(qcow2 ? QStringLiteral("（qcow2）") : QString())
             .arg(path));
}

void MainWindow::onAttachIso()
{
    const QString p = QFileDialog::getOpenFileName(this, QStringLiteral("选择 ISO / 只读镜像"),
                                                   QDir::homePath(),
                                                   QStringLiteral("ISO (*.iso);;所有文件 (*)"));
    if (p.isEmpty())
        return;
    auto *it = new QListWidgetItem(p + QStringLiteral("  [ISO]"), m_disks);
    it->setData(Qt::UserRole, true);
    it->setData(Qt::UserRole + 1, true);
    it->setData(Qt::UserRole + 2, p);
    m_disks->setCurrentItem(it);
    persistForm();
    note(QStringLiteral("已挂载只读镜像（--scsi-block）：%1").arg(p));
}

void MainWindow::onAttachDisk()
{
    const QString n = selectedName();
    const QString p = QFileDialog::getOpenFileName(this, QStringLiteral("选择磁盘镜像"),
                                                   n.isEmpty() ? QDir::homePath()
                                                               : Store::vmDir(n),
                                                   QString());
    if (p.isEmpty())
        return;

    // 同一个文件不要重复添加
    for (int i = 0; i < m_disks->count(); ++i)
        if (m_disks->item(i)->data(Qt::UserRole + 2).toString() == p) {
            note(QStringLiteral("这个镜像已经添加过了：%1").arg(p), true);
            return;
        }

    auto *it = new QListWidgetItem(p, m_disks);
    it->setData(Qt::UserRole, false);
    it->setData(Qt::UserRole + 1, false);
    it->setData(Qt::UserRole + 2, p);
    m_disks->setCurrentItem(it);

    // 立刻落盘：列表控件的状态不是真值，loadForm() 会用磁盘上的配置重建它，
    // 不马上保存的话，下一次界面刷新就会把这个条目悄悄冲掉。
    persistForm();
    note(QStringLiteral("已添加磁盘：%1").arg(QFileInfo(p).fileName()));
}

void MainWindow::onRemoveDisk()
{
    QListWidgetItem *it = m_disks->currentItem();
    if (!it)
        return;
    const QString gone = it->data(Qt::UserRole + 2).toString();
    delete m_disks->takeItem(m_disks->row(it));
    persistForm();
    note(QStringLiteral("已移除：%1").arg(QFileInfo(gone).fileName()));
}

// ---------------------------------------------------------------- 运行态

void MainWindow::onOutput(const QString &name, const QByteArray &data)
{
    // 落盘：<vm 目录>/console.log
    QFile log(Store::consoleLog(name));
    if (log.open(QIODevice::Append))
        log.write(data);

    if (name != selectedName())
        return;
    m_console->moveCursor(QTextCursor::End);
    m_console->insertPlainText(QString::fromUtf8(data));
    m_console->moveCursor(QTextCursor::End);
}

void MainWindow::onStateChanged(const QString &name, bool running)
{
    reloadList(name);
    if (name == selectedName())
        setRunningUi(running);
}

void MainWindow::onSendInput()
{
    const QString n = selectedName();
    if (n.isEmpty())
        return;
    m_backend.sendInput(n, m_input->text().toUtf8() + '\n');
    m_input->clear();
}

void MainWindow::setRunningUi(bool running)
{
    if (m_actStart)
        m_actStart->setEnabled(!running);
    if (m_actStop)
        m_actStop->setEnabled(running);
    if (m_actRestart)
        m_actRestart->setEnabled(true);
}

void MainWindow::note(const QString &msg, bool error)
{
    if (m_status)
        m_status->setText(error ? QStringLiteral("⚠ ") + msg : msg);
}

void MainWindow::onRefreshStats()
{
    if (!m_statsLabel)
        return;

    const QString n = selectedName();
    if (n.isEmpty() || !m_backend.running(n)) {
        m_statsLabel->clear();
        return;
    }

    const VmStats s = m_backend.stats(n);
    if (!s.valid) {
        m_statsLabel->setText(QStringLiteral("pid %1").arg(s.pid));
        return;
    }

    const double rssMiB = s.rssBytes / 1048576.0;
    const double peakMiB = s.peakBytes / 1048576.0;
    const long hz = sysconf(_SC_CLK_TCK);
    const double cpuSec = s.cpuTicks / double(hz);
    const int upSec = int(s.uptimeMs / 1000);

    // 平均 CPU 占用 = 累计 CPU 时间 / 运行时长
    const double cpuPct = upSec > 0 ? (cpuSec / upSec * 100.0) : 0.0;

    m_statsLabel->setText(
        QStringLiteral("内存 %1 MiB（峰 %2）  ·  CPU %3%  ·  %4 线程  ·  运行 %5")
            .arg(rssMiB, 0, 'f', 0)
            .arg(peakMiB, 0, 'f', 0)
            .arg(cpuPct, 0, 'f', 0)
            .arg(s.threads)
            .arg(formatDuration(upSec)));
}

void MainWindow::onRestart()
{
    const QString n = selectedName();
    if (n.isEmpty())
        return;
    if (!m_backend.running(n)) {
        onStart();
        return;
    }

    const VmConfig vm = Store::load(n);
    note(QStringLiteral("正在重启「%1」…").arg(n));
    m_backend.stop(n);

    // 等 crosvm 真正退出再拉起，否则会撞上「已经在运行」
    auto *t = new QTimer(this);
    t->setInterval(250);
    int tries = 0;
    connect(t, &QTimer::timeout, this, [this, t, vm, &tries]() {
        if (!m_backend.running(vm.name) || ++tries > 40) {
            t->stop();
            t->deleteLater();
            QString err;
            if (m_backend.start(vm, &err))
                note(QStringLiteral("已重启「%1」").arg(vm.name));
            else
                note(QStringLiteral("重启失败：%1").arg(err), true);
        }
    });
    t->start();
}

void MainWindow::updateMemHint()
{
    if (!m_memHint || !m_mem)
        return;

    // MemAvailable 是内核估算的“还能给新分配用多少”，比 MemFree 靠谱
    QFile f(QStringLiteral("/proc/meminfo"));
    qint64 availMiB = 0;
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd()) {
            const QByteArray line = f.readLine();
            if (line.startsWith("MemAvailable:"))
                availMiB = line.mid(13).trimmed().split(' ').value(0).toLongLong() / 1024;
        }
    }

    const int want = m_mem->value();
    if (availMiB <= 0) {
        m_memHint->clear();
        return;
    }

    if (want > availMiB) {
        m_memHint->setText(QStringLiteral("⚠ 超过可用 %1 MiB").arg(availMiB));
        m_memHint->setStyleSheet(QStringLiteral("color:#ff8a80;"));
    } else if (want > availMiB * 3 / 4) {
        m_memHint->setText(QStringLiteral("可用 %1 MiB（偏紧）").arg(availMiB));
        m_memHint->setStyleSheet(QStringLiteral("color:#ffb74d;"));
    } else {
        m_memHint->setText(QStringLiteral("可用 %1 MiB").arg(availMiB));
        m_memHint->setStyleSheet(QStringLiteral("color:#8ab4f8;"));
    }
}

void MainWindow::updateGpuAvailability()
{
    const bool ok = m_backend.gpuSupported();
    m_gpu->setEnabled(ok);
    if (!ok) {
        m_gpu->setChecked(false);
        m_gpu->setToolTip(QStringLiteral(
            "当前 crosvm 未编译 gpu 功能（--gpu 不被识别）。重编时把 gpu 加进 features 即可。"));
    } else {
        m_gpu->setToolTip(QStringLiteral("crosvm 会弹出独立窗口显示客户机画面"));
    }
}

void MainWindow::checkShmem()
{
    auto readShmem = []() -> QString {
        QFile f(QStringLiteral("/sys/kernel/mm/transparent_hugepage/shmem_enabled"));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromLatin1(f.readAll()).simplified();
    };
    auto off = [](const QString &s) {
        return s.contains(QStringLiteral("[never]")) || s.contains(QStringLiteral("[deny]"));
    };

    QString s = readShmem();
    if (s.isEmpty())
        return;

    // 若关闭且有 daemon，自动打开（内存 >1G 的 VM 必需）
    if (off(s) && m_backend.daemonAvailable() && m_backend.setShmemThp(QStringLiteral("advise"))) {
        s = readShmem();
        note(QStringLiteral("已自动打开 shmem 大页：%1").arg(s));
        return;
    }

    if (off(s)) {
        note(QStringLiteral("⚠ shmem 大页被关闭（%1）——内存 >1G 的 VM 会被 RM 拒绝；"
                            "执行 sudo tee 写 advise 到 "
                            "/sys/kernel/mm/transparent_hugepage/shmem_enabled")
                 .arg(s),
             true);
    } else {
        note(QStringLiteral("就绪。shmem 大页：%1").arg(s));
    }
}

// ---------------------------------------------------------------- 新增功能

void MainWindow::onAddSharedDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择要共享给客户机的宿主目录"), QDir::homePath());
    if (dir.isEmpty())
        return;
    bool ok = false;
    const QString tag = QInputDialog::getText(this, QStringLiteral("共享标签"),
                                              QStringLiteral("客户机里用这个标签挂载："),
                                              QLineEdit::Normal, QFileInfo(dir).fileName(), &ok);
    if (!ok || tag.isEmpty())
        return;
    auto *it = new QListWidgetItem(dir + QStringLiteral("  →  ") + tag, m_sharedDirs);
    it->setData(Qt::UserRole, dir);
    it->setData(Qt::UserRole + 1, tag);
    it->setData(Qt::UserRole + 2, false);
    m_sharedDirs->setCurrentItem(it);
    persistForm();
    note(QStringLiteral("已添加共享目录：%1 → %2").arg(dir, tag));
}

void MainWindow::onRemoveSharedDir()
{
    QListWidgetItem *it = m_sharedDirs->currentItem();
    if (!it)
        return;
    delete m_sharedDirs->takeItem(m_sharedDirs->row(it));
    persistForm();
    note(QStringLiteral("已移除共享目录"));
}

void MainWindow::onDiskResize()
{
    QListWidgetItem *it = m_disks->currentItem();
    if (!it) {
        note(QStringLiteral("先在列表里选一个磁盘"), true);
        return;
    }
    const QString path = it->data(Qt::UserRole + 2).toString();
    const QFileInfo fi(path);
    const qint64 curGib = fi.size() / (1024LL * 1024 * 1024);
    bool ok = false;
    const int gib = QInputDialog::getInt(
        this, QStringLiteral("扩容 / 调整大小"),
        QStringLiteral("新的镜像大小（GiB）；当前约 %1 GiB（%2 字节）")
            .arg(curGib)
            .arg(fi.size()),
        int(qMax<qint64>(1, curGib)), 1, 8192, 1, &ok);
    if (!ok)
        return;
    const qint64 want = qint64(gib) * 1024 * 1024 * 1024;
    if (want < fi.size()
        && QMessageBox::warning(this, QStringLiteral("缩小镜像"),
                                QStringLiteral("缩小可能损坏镜像里的文件系统，确定继续？"))
               != QMessageBox::Yes)
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite) || !f.resize(want)) {
        note(QStringLiteral("调整失败：%1").arg(path), true);
        return;
    }
    note(QStringLiteral("已把镜像调整为 %1 GiB").arg(gib));
}

void MainWindow::onDiskClone()
{
    QListWidgetItem *it = m_disks->currentItem();
    if (!it) {
        note(QStringLiteral("先在列表里选一个磁盘"), true);
        return;
    }
    const QString src = it->data(Qt::UserRole + 2).toString();
    const QString dst = QFileDialog::getSaveFileName(this, QStringLiteral("克隆到"),
                                                     src + QStringLiteral(".clone"));
    if (dst.isEmpty())
        return;
    if (QFile::exists(dst)
        && QMessageBox::question(this, QStringLiteral("覆盖"),
                                 QStringLiteral("目标已存在，覆盖？"))
               != QMessageBox::Yes)
        return;
    if (QFile::remove(dst) || !QFile::exists(dst)) {
        if (QFile::copy(src, dst)) {
            note(QStringLiteral("已克隆到：%1").arg(dst));
            return;
        }
    }
    note(QStringLiteral("克隆失败"), true);
}

void MainWindow::onShowCommand()
{
    QStringList full = m_backend.launchPrefix();
    full << m_backend.crosvmPath();
    full += m_backend.fullArgs(formConfig(false));

    QString pretty;
    for (const QString &p : full) {
        const bool quote = p.contains(QLatin1Char(' ')) || p.contains(QLatin1Char('['))
                           || p.contains(QLatin1Char(':'));
        pretty += quote ? QLatin1Char('\'') + p + QStringLiteral("' ") : p + QLatin1Char(' ');
    }

    QDialog d(this);
    d.setWindowTitle(QStringLiteral("将要执行的命令"));
    d.resize(qMin(760, width()), qMin(400, height()));
    auto *v = new QVBoxLayout(&d);
    auto *te = new QPlainTextEdit(pretty, &d);
    te->setReadOnly(true);
    v->addWidget(te, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, &d);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    d.exec();
}

QString MainWindow::selfCheckText() const
{
    auto read = [](const QString &p, const QString &def = QString()) -> QString {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly))
            return def;
        return QString::fromLatin1(f.readAll()).simplified();
    };
    auto has = [](const QString &p) { return QFileInfo::exists(p); };
    const QString yes = QStringLiteral("✅");
    const QString no = QStringLiteral("❌");

    QString s;
    s += QStringLiteral("=== gyvm 环境自检 ===\n\n");

    s += QStringLiteral("[Gunyah]\n");
    s += QStringLiteral("  /dev/gunyah            : %1\n")
             .arg(has(QStringLiteral("/dev/gunyah")) ? QStringLiteral("存在 ") + yes
                                                     : QStringLiteral("缺失 ") + no);
    s += QStringLiteral("  hypervisor DT 节点     : %1\n")
             .arg(has(QStringLiteral("/proc/device-tree/hypervisor")) ? QStringLiteral("有")
                                                                      : QStringLiteral("无"));
    s += QStringLiteral("  模块 gunyah            : %1\n")
             .arg(has(QStringLiteral("/sys/module/gunyah")) ? QStringLiteral("已加载 ") + yes
                                                            : QStringLiteral("未加载 ") + no);

    s += QStringLiteral("\n[crosvm]\n");
    s += QStringLiteral("  路径                   : %1\n").arg(m_backend.crosvmPath());
    s += QStringLiteral("  存在                   : %1\n")
             .arg(has(m_backend.crosvmPath()) ? yes : no);
    s += QStringLiteral("  支持 --gpu（图形窗口） : %1\n")
             .arg(m_backend.gpuSupported()
                      ? QStringLiteral("支持 ") + yes
                      : QStringLiteral("不支持（需重编 crosvm 加 gpu feature）"));

    s += QStringLiteral("\n[内存 / 大页]\n");
    s += QStringLiteral("  shmem 大页             : %1\n")
             .arg(read(QStringLiteral("/sys/kernel/mm/transparent_hugepage/shmem_enabled"),
                       QStringLiteral("(无此文件)")));
    s += QStringLiteral("  匿名大页               : %1\n")
             .arg(read(QStringLiteral("/sys/kernel/mm/transparent_hugepage/enabled"),
                       QStringLiteral("(无此文件)")));
    s += QStringLiteral("  （shmem 用 advise 即可：crosvm 的 --hugepages 会对自己的 memfd\n"
                        "    发 MADV_HUGEPAGE，大页只落在 guest 内存上）\n");

    s += QStringLiteral("\n[提权]\n");
    s += QStringLiteral("  特权 daemon (gyvmd)    : %1\n")
             .arg(m_backend.daemonAvailable()
                      ? QStringLiteral("可用 ") + yes + QStringLiteral("（免密码框）")
                      : QStringLiteral("未运行（回退到下面的前缀提权）"));
    s += QStringLiteral("  前缀                   : %1\n")
             .arg(m_backend.launchPrefix().isEmpty() ? QStringLiteral("(无，直接运行)")
                                                      : m_backend.launchPrefix().join(QLatin1Char(' ')));

    s += QStringLiteral("\n[存储]\n");
    s += QStringLiteral("  VM 定义目录            : %1\n").arg(Store::vmsDir());
    s += QStringLiteral("  磁盘 / 共享目录在 vm.json 里配置\n");
    return s;
}

void MainWindow::onSelfCheck()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("环境自检"));
    d.resize(qMin(780, width()), qMin(680, height()));
    auto *v = new QVBoxLayout(&d);
    auto *te = new QPlainTextEdit(selfCheckText(), &d);
    te->setReadOnly(true);
    v->addWidget(te, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, &d);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    d.exec();
}
