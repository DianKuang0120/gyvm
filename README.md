# gyvm — Gunyah 虚拟机管理器（C++ / Qt6）

一个用**纯 C++（Qt6 Widgets）**写的虚拟机管理器，目标是把"在手机上开一台 Gunyah 虚拟机"
变成点几下的事，像 VMware 一样：左侧机器列表、右侧配置、独立的图形窗口跑客户机画面。

> 全新工程，位于 `~/Code/gyvm`。与 `~/Gunyah/librevm`（Rust + GTK4）是两套独立实现，
> 这个版本走 **C++/Qt** 路线。

## 现在能做什么

| 能力 | 状态 |
|---|---|
| 虚拟机定义（JSON，存在 `~/.local/share/gyvm/vms/<name>/vm.json`） | ✅ |
| 图形界面：列表 + 配置页（可滚动）+ 控制台页 | ✅ |
| 深色主题（贴合 KDE Breeze Dark）+ breeze 图标 + 圆角卡片 | ✅ |
| 自适应屏幕尺寸（竖屏/窄屏友好） | ✅ |
| 新建 / 保存 / 删除 | ✅ |
| 启动 / 停止（`QProcess` 拉起 crosvm；经 `sudo -A` + ksshaskpass 图形提权） | ✅ |
| 串口控制台（crosvm stdout → 窗口；输入 → 客户机 stdin） | ✅ |
| **图形窗口**（virtio-gpu，独立窗口，抓取键鼠，可交互） | ✅ |
| **控制台日志落盘**（`<vm>/console.log`） | ✅ |
| **磁盘镜像**：创建（raw / **qcow2**）/ 扩容 / 克隆 / 挂载 / 移除 | ✅ |
| **ISO / 只读镜像**：`--scsi-block <path>,ro=true`（挂载 ISO 按钮） | ✅ |
| **UEFI 变量存储**：`--pflash path=<vars.fd>,block_size=4096`（UEFI 启动必需） | ✅ |
| **pstore**：`--pstore`，客户机内核日志跨重启保留 | ✅ |
| **共享目录（virtio-fs）**：`--shared-dir <dir>:<tag>:type=fs:cache=always` | ✅ |
| **CLI（gyvmctl）**：`list` / `show` / `start` / `stop` / `rm` / `create` | ✅ |
| **环境自检**：/dev/gunyah、gunyah 模块、crosvm 的 gpu 支持、shmem 大页、提权前缀 | ✅ |
| **查看将要执行的命令**（一键显示完整 crosvm 命令行） | ✅ |
| **D-Bus 特权 daemon（gyvmd）**：GUI 以普通用户跑，启动/停止走 root daemon，**免密码框** | ✅ |
| **自动打开 shmem 大页**（有 daemon 时开机自动 `advise`） | ✅ |
| 受保护 VM / 大页（本机必须 `--protected-vm-without-firmware` + `--hugepages`） | ✅ 默认开启 |
| **重启按钮**（停止后自动拉起，配置改动直接生效） | ✅ 新增 |
| **实时资源占用**（状态栏显示 RSS / 峰值 / CPU% / 线程数 / 运行时长） | ✅ 新增 |
| **启动前预检**（内核/initrd/磁盘缺失、无启动介质、shmem 大页未开，提前弹窗） | ✅ 新增 |
| **内存容量提示**（实时显示宿主机 MemAvailable，超标变红） | ✅ 新增 |
| **克隆虚拟机**（可选一并复制磁盘和 UEFI 变量，避免两台共写同一镜像） | ✅ 新增 |
| **Gunyah 驱动状态窗口**（GUI 内直接读 `/sys/kernel/debug/gunyah`，看内存/资源/RPC 延迟） | ✅ 新增 |
| **打开控制台日志**（一键用系统默认程序打开 `console.log`） | ✅ 新增 |
| 网络：virtio-net（tap） | 🟡 参数已接，**本机会导致 crosvm 卡死**，暂缓 |
| 宿主 tap/IP 转发自动配置（提权） | ⬜ TODO |
| 快照 / 模板 / i18n / 控制台终端仿真 | ⬜ TODO |

### 新增功能说明

**实时资源占用**（状态栏右下角，每 1.5 秒刷新）

```
内存 1024 MiB（峰 1180）  ·  CPU 23%  ·  38 线程  ·  运行 4m 12s
```

数据直接读 `/proc/<crosvm pid>/status` 和 `/proc/<pid>/stat`，不依赖客户机。
走 daemon 时通过 D-Bus 的 `Pid()` 拿进程号。CPU% 是「累计 CPU 时间 ÷ 运行时长」的平均值。

**启动前预检**会把原本只在 dmesg 里看得到的失败提前：

| 检查 | 提示 |
|---|---|
| 内核/initrd/UEFI 变量文件不存在 | 直接弹窗，不进启动流程 |
| 没有 initrd 也没有磁盘 | 「客户机会直接 panic」 |
| 磁盘镜像路径失效 | 列出具体文件 |
| shmem 大页为 `never` 且内存 > 1 GiB | 询问是否自动改成 `advise` |

**克隆虚拟机**（文件 → 克隆虚拟机…）

- 磁盘默认复制到新 VM 目录，两台可以同时跑
- 选「否」则共用镜像，此时两台不能同时启动
- UEFI 变量文件（`pflash`）总是复制，否则会互相覆盖启动项
- 有 `qemu-img` 时用它转换（qcow2 保持 qcow2），否则退回 `QFile::copy`

**Gunyah 驱动状态**（设置 → Gunyah 驱动状态…）

把 `~/Gunyah/README.md` 里描述的所有 debugfs 文件整合到一个窗口：

```
══════ VM 0 ══════

── state ──
vmid:     0
status:   running (3)
...
── memory ──
label  gpa               size        pages  type   handle
...
── rm ──
RPC (synchronous request/reply):
  message     count  errors  avg_us  max_us  last_us
  ...
Notifications (asynchronous, ordered workqueue):
  received:          148
  ...
```

点「刷新」重新读取。VM 卡住时不必再开终端敲命令。


## 用模板一键建 Ubuntu（推荐）

`~/Gunyah/images/<模板名>/` 下放三样东西就是一个模板：

```
ubuntu-2404/
├── Image        未压缩的 arm64 内核（crosvm 不认压缩过的 vmlinuz）
├── initrd.img   对应的 initramfs
└── rootfs.qcow2 系统盘
```

菜单 **文件 → 从模板新建…**，选模板、填名字即可。系统盘会被**复制**一份到新 VM 目录，
所以同一个模板可以开多台互不影响。

### 现成的 Ubuntu 24.04 LTS 模板

已经准备好放在 `~/Gunyah/images/ubuntu-2404/`：

| 文件 | 来源 |
|---|---|
| `Image` | cloud image 里 `/boot/vmlinuz-6.8.0-147-generic` 解压后 |
| `initrd.img` | cloud image 里 `/boot/initrd.img-6.8.0-147-generic` |
| `rootfs.qcow2` | 中科大 `noble-server-cloudimg-arm64.img` |

root 密码是 `114514`，串口（`ttyS0`）自动登录。

### 手工从 cloud image 做一个模板

```bash
# 1. 下载（中科大镜像）
cd ~/Gunyah/images
curl -LO https://mirrors.ustc.edu.cn/ubuntu-cloud-images/noble/current/noble-server-cloudimg-arm64.img

# 2. 转 raw 方便挂载
qemu-img convert -O raw noble-server-cloudimg-arm64.img noble.raw

# 3. 挂 /boot，取出内核和 initrd
sudo mount -o loop,offset=$((206848*512)),sizelimit=$((1890305*512)) noble.raw /mnt
sudo cp /mnt/vmlinuz-6.8.0-147-generic ~/Gunyah/images/ubuntu-2404/vmlinuz
sudo cp /mnt/initrd.img-6.8.0-147-generic ~/Gunyah/images/ubuntu-2404/initrd.img
sudo umount /mnt

# 4. 解压内核——crosvm 只认未压缩的 arm64 Image
gunzip -c vmlinuz > Image

# 5. 设 root 密码（cloud image 默认锁定 root）
sudo mount -o loop,offset=$((2099200*512)),sizelimit=$((5240799*512)) noble.raw /mnt
HASH=$(openssl passwd -6 你的密码)
sudo sed -i "s|^root:[^:]*:|root:$HASH:|" /mnt/etc/shadow

# 6. 串口自动登录
sudo mkdir -p /mnt/etc/systemd/system/serial-getty@ttyS0.service.d
sudo tee /mnt/etc/systemd/system/serial-getty@ttyS0.service.d/autologin.conf <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --keep-baud 115200,38400,9600 %I $TERM
EOF
sudo umount /mnt

# 7. 内核命令行要用 UUID 指定 root
sudo blkid -p -o export --offset $((2099200*512)) --size $((5240799*512)) noble.raw | grep UUID
```

启动参数里加：

```
root=UUID=<上一步的 UUID> rw panic=0 systemd.mask=systemd-networkd-wait-online.service
```

`systemd.mask` 是必须的——没有网卡时 Ubuntu 会卡在 `networkd-wait-online` 一分多钟。

## 用 UEFI + ISO 启动 Ubuntu（配方）

> 目标：用 edk2（或 DroidVM 的 Gunyah 专用 UEFI）作为 payload，从 ISO 引导 arm64 发行版。

1. **UEFI 固件**：准备一个 arm64 edk2 固件（如 `AAVMF_CODE.fd`，或 DroidVM 的 Gunyah 版），
   在 gyvm 里把它填到 **「内核 Image」**（就是 crosvm 的位置参数/payload）；
2. **UEFI 变量**：点「UEFI 变量」旁的 **选择/新建**，建一个 `vars.fd`（空文件即可，
   crosvm 会用 `--pflash` 提供 UEFI 变量存储）；
3. **ISO**：点 **挂载 ISO** 选中你的 Ubuntu arm64 ISO
   （crosvm 这个版本没有真正的 CD-ROM，会以 **只读 SCSI 盘** `--scsi-block ...,ro=true` 暴露——
   能否被固件当可引导介质识别取决于固件，需实测）；
4. **安装盘**：点「创建镜像」（可选 **qcow2**）建一个系统盘，用来装 Ubuntu；
5. 勾上「图形窗口」→ 启动。

> 附：`--pstore` 打开后，客户机内核日志会留在 `<vm>/pstore.bin`，崩溃也翻得到。

## 构建

需要 Qt6（6.5+）、CMake、Ninja、GCC/Clang。本机（ArchLinux aarch64）已全部具备。

```sh
cd ~/Code/gyvm
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/gyvm          # 或 ./run.sh
```

依赖（Debian 系参考）：`qt6-base-dev cmake ninja-build g++`。

## 运行前须知（本机 Gunyah 的三个硬要求）

> **有特权 daemon 时，下面第 1、3 条由 daemon 自动处理**（见「守护进程」一节），你基本什么都不用管。

0. **（推荐）先装守护进程** —— 见下方「守护进程」，之后启动 VM 不再弹密码框。
1. **必须受保护 VM**：非受保护 VM 会让**主机 panic**，所以默认带
   `--protected-vm-without-firmware`（走 LEND + 内存隔离）。
2. **大页只作用于虚拟机进程**：crosvm 的 guest 内存是 memfd(shmem)，
   `--hugepages` 会让它对自己的 memfd 发 `MADV_HUGEPAGE`。所以系统级
   `shmem_enabled` 用默认的 `advise` 即可，**不需要也不建议改成 `always`**——
   `advise` 只影响主动请求的进程，系统里其它 tmpfs 使用者不受影响。

   | `shmem_enabled` | crosvm 带 `--hugepages` | 说明 |
   |---|---|---|
   | `advise`（默认） | ✅ 1G / 2G / 4G 均正常 | 推荐 |
   | `never` / `deny` | ❌ | 连 `madvise` 也被拒 |
   | `always` | ✅ | 但**所有** tmpfs 都会被强制大页，没必要 |

   验证大页真的生效：

   ```bash
   grep ShmemHugePages /proc/meminfo   # VM 运行时应大于 0
   ```

   用 `advise` 时，只有 crosvm 的 guest 内存会贡献这个数字，其它进程不受影响。
3. **需要 root**：`/dev/gunyah` 目前 `root:root 0600`。有 daemon 就由它提权；
   否则用「设置 → 首选项」里的**提权前缀**（`sudo -A` + ksshaskpass 图形密码框）。

图形窗口还需要 crosvm 能连到你的 Wayland（程序会自动带上
`--wayland-sock $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY`）。

## 守护进程（免密码提权）

`gyvmd` 以 root 运行，通过**系统总线** `org.gyvm.Daemon1` 替 GUI 拉起 crosvm。
装上后，GUI 以普通用户启动 VM / 停止 VM **都不再弹密码框**。

一次安装（需要一次 sudo）：

```sh
cd ~/Code/gyvm
sudo install -m755 build/gyvmd                                   /usr/local/bin/gyvmd
sudo install -m644 packaging/gyvmd.service                       /usr/lib/systemd/system/gyvmd.service
sudo install -m644 packaging/org.gyvm.Daemon1.conf               /usr/share/dbus-1/system.d/org.gyvm.Daemon1.conf
sudo systemctl daemon-reload
sudo systemctl enable --now gyvmd
sudo systemctl reload dbus       # 或 dbus-broker，让 D-Bus 策略生效
```

验证：

```sh
busctl --system call org.gyvm.Daemon1 /org/gyvm/Daemon org.gyvm.Daemon1 Version
# s "gyvmd 0.1.0"
```

`gyvm` 启动时会自动探测：有 daemon 就用它（免密码），没有就回退到本地的
`sudo -A` + ksshaskpass 图形提权。用没用到看「设置 → 环境自检」。

> D-Bus 策略默认放行 `gyvm` 组和安装时的用户名（`packaging/org.gyvm.Daemon1.conf`）。

## 客户机内核

需要一个带 **8250/PL011 串口**、`CONFIG_DRM_VIRTIO_GPU=y`（图形窗口才需要）、
`CONFIG_VIRTIO_*=y` 的 arm64 Image（`ARCH_QCOM` 关掉），外加一个 initramfs。
最小配方见 `~/Mainline-Kernel/Gunyah/Gunyah-VM-完整记录.md`。

## 源码结构

```
src/
├── vmconfig.h        VmConfig / DiskAttach / SharedDir：定义 + JSON（反）序列化
├── store.h           存储：~/.local/share/gyvm/vms/<name>/vm.json
├── backend.{h,cpp}   crosvm 后端：buildArgs() 命令行生成、进程生命周期、VmStats
├── mainwindow.{h,cpp} 主窗口：列表 / 配置页 / 控制台 / 磁盘 / 克隆 / 驱动状态
├── main.cpp          入口
├── daemon.h + gyvmd.cpp   特权 D-Bus 守护进程
└── gyvmctl.cpp       CLI

关键接口：
  Backend::buildArgs(vm)   纯函数，配置 → crosvm 参数（可单独测试）
  Backend::stats(name)     /proc 解析，返回 VmStats{rss, peak, cpu, threads, uptime}
  Backend::pidOf(name)     本地 QProcess 或 D-Bus Pid()
```

`Backend::buildArgs()` 是纯函数，单独可测——它产出的就是：
`crosvm --no-syslog run --hypervisor gunyah --disable-sandbox --protected-vm-without-firmware
--mem N --cpus N --hugepages [--gpu backend=2d,displays=[[mode=windowed[W,H]]] --wayland-sock …]
[--net tap-name=…] --serial type=stdout,hardware=serial,console=true,stdin=true --params panic=0
[--initrd …] [--disk …] <kernel>`。

## 路线图

1. **宿主网络自动化**：创建 tap、开 `proxy_arp` / `arp_ignore`、`ip_forward`（提权助手）。
2. **独立的 VM 显示窗口**：把 `--gpu` 的窗口与 GUI 生命周期绑定（关标签即停 VM 等）。
3. **磁盘管理**：qcow2、扩容、克隆、挂到宿主 fdisk/挂载查看。
4. **快照 / 模板 / 克隆**。
5. **提权助手**（polkit/DBus）：GUI 保持普通用户，特权动作走 daemon。
6. i18n、图标、打包（.desktop / PKGBUILD / deb）。
