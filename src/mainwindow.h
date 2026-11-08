// gyvm - 主窗口
#pragma once

#include "backend.h"
#include "vmconfig.h"

#include <QMainWindow>

class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onNewVm();
    void onSaveVm();
    void onDeleteVm();
    void onStart();
    void onStop();
    void onBrowseKernel();
    void onBrowseInitrd();
    void onCreateDisk();
    void onAttachDisk();
    void onAttachIso();
    void onRemoveDisk();
    void onDiskResize();
    void onDiskClone();
    void onAddSharedDir();
    void onRemoveSharedDir();
    void onShowCommand();
    void onSelfCheck();
    void onSelectionChanged();
    void onOutput(const QString &name, const QByteArray &data);
    void onStateChanged(const QString &name, bool running);
    void onSendInput();
    void onPreferences();
    void onRefreshStats();
    void onRestart();
    void onCloneVm();
    void onOpenLog();
    void onGunyahInfo();

private:
    void buildUi();
    void buildActions();
    void loadSettings();
    void saveSettings();
    void reloadList(const QString &select = QString());
    void loadForm(const VmConfig &vm);
    VmConfig formConfig(bool forSave) const;
    QString selectedName() const;
    void setRunningUi(bool running);
    void note(const QString &msg, bool error = false);
    void updateGpuAvailability();
    void updateMemHint();
    void checkShmem();
    // 把当前表单内容（含磁盘/共享目录列表）立即写回 vm.json
    void persistForm();
    QString selfCheckText() const;

    Backend m_backend;

    QListWidget *m_list = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_kernel = nullptr;
    QLineEdit *m_initrd = nullptr;
    QSpinBox *m_mem = nullptr;
    QSpinBox *m_cpus = nullptr;
    QCheckBox *m_protected = nullptr;
    QCheckBox *m_huge = nullptr;
    QCheckBox *m_gpu = nullptr;
    QCheckBox *m_net = nullptr;
    QLineEdit *m_tap = nullptr;
    QListWidget *m_disks = nullptr;
    QListWidget *m_sharedDirs = nullptr;
    QLineEdit *m_pflash = nullptr;
    QCheckBox *m_pstore = nullptr;
    QPlainTextEdit *m_console = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_memHint = nullptr;
    QTimer *m_statsTimer = nullptr;

    QAction *m_actStart = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actRestart = nullptr;
};
