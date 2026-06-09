# WSL2 NFS 服务配置 — 供 T32 真机挂载 build

> **环境**：华为笔记本，Windows 11 + WSL2 (Ubuntu)，已验证 nfs-kernel-server 可正常工作。  
> **目的**：将 WSL2 内的 `build/` 目录通过 NFS 共享给局域网内的 T32 设备，用于真机加载固件和调试。

---

## 1. 网络环境

| 项目 | 值 |
|------|-----|
| WSL 版本 | WSL2 |
| WSL 主机名 | Ubuntu |
| WSL 物理网卡 (`eth1`) | `192.168.31.200/24` |
| Windows WLAN | `192.168.31.200`（与 WSL 共享 IP，说明启用了 mirrored/桥接模式） |
| T32 设备网段 | `192.168.31.0/24` |

**结论**：T32 设备可直接通过 `192.168.31.200` 访问 WSL2 服务，无需额外的端口转发。

---

## 2. WSL2 侧：安装并配置 NFS 服务器

### 2.1 安装 nfs-kernel-server

```bash
sudo apt-get update
sudo apt-get install -y nfs-kernel-server
```

### 2.2 配置导出目录

编辑 `/etc/exports`：

```bash
sudo tee /etc/exports << 'EOF'
/home/zengping/projects/hc_t32/code/t32/build 192.168.31.0/24(rw,sync,no_subtree_check,no_root_squash,insecure)
EOF
```

参数说明：

| 参数 | 含义 |
|------|------|
| `rw` | 读写权限 |
| `sync` | 同步写入，保证数据完整性 |
| `no_subtree_check` | 禁用子树检查，提升性能 |
| `no_root_squash` | 允许 root 用户保持 root 权限（嵌入式调试常用） |
| `insecure` | **关键**：允许客户端使用 >1024 的端口挂载，嵌入式 Linux 默认行为 |

### 2.3 启动/重启服务

```bash
sudo exportfs -ra          # 重新加载 exports 配置
sudo service nfs-kernel-server restart
# 或
sudo systemctl restart nfs-server
```

### 2.4 验证服务状态

```bash
sudo exportfs -v           # 查看已导出的目录
showmount -e localhost     # 查看本机可挂载的目录
sudo service nfs-kernel-server status
```

---

## 3. Windows 侧：防火墙放行 NFS 端口

在 **PowerShell（管理员）** 中执行：

```powershell
# TCP 规则
netsh advfirewall firewall add rule name="NFS Server TCP" dir=in action=allow protocol=tcp localport=2049,111

# UDP 规则
netsh advfirewall firewall add rule name="NFS Server UDP" dir=in action=allow protocol=udp localport=2049,111
```

涉及端口：

| 端口 | 协议 | 用途 |
|------|------|------|
| 2049 | TCP/UDP | NFS 数据传输 |
| 111 | TCP/UDP | RPC portmap（NFSv3 必需） |

---

## 4. T32 侧：挂载 NFS 目录

### 4.1 手动挂载

在 T32 设备的 shell 中：

```bash
mkdir -p /mnt/nfs_build

mount -t nfs -o nolock,vers=3 \
    192.168.31.200:/home/zengping/projects/hc_t32/code/t32/build \
    /mnt/nfs_build

ls /mnt/nfs_build
```

挂载参数说明：

| 参数 | 说明 |
|------|------|
| `nolock` | 禁用文件锁，嵌入式环境 NLM 常不可用 |
| `vers=3` | 使用 NFSv3，兼容性最好；如失败可尝试 `vers=4` |

### 4.2 开机自动挂载（/etc/fstab）

在 T32 设备的 `/etc/fstab` 中添加：

```fstab
192.168.31.200:/home/zengping/projects/hc_t32/code/t32/build /mnt/nfs_build nfs defaults,nolock,vers=3 0 0
```

---

## 5. 常见问题

### Q1: T32 挂载时报 `Connection refused`
- 检查 Windows 防火墙是否放行了 2049/111 端口
- 检查 WSL2 内 `sudo service nfs-kernel-server status` 是否正常运行
- 确认 T32 和笔记本在同一网段（`192.168.31.x`）

### Q2: T32 挂载时报 `Permission denied`
- 确认 `/etc/exports` 中加入了 `insecure` 选项
- 检查 `build/` 目录的本地权限是否允许访问

### Q3: WSL2 重启后 NFS 服务未自动启动
```bash
sudo systemctl enable nfs-server   # 设置开机自启
```

### Q4: 挂载后文件显示为 `nobody:nogroup`
- 如果希望保留 UID/GID 映射，确保 `/etc/exports` 中有 `no_root_squash`
- 或统一 T32 和 WSL 的 uid/gid

---

## 6. 速查命令

| 操作 | 命令 |
|------|------|
| WSL 重启 NFS | `sudo service nfs-kernel-server restart` |
| WSL 查看导出 | `sudo exportfs -v` |
| WSL 查看挂载者 | `cat /var/lib/nfs/rmtab` |
| T32 挂载 | `mount -t nfs -o nolock,vers=3 192.168.31.200:/path /mnt/nfs_build` |
| T32 卸载 | `umount /mnt/nfs_build` |
| T32 查看挂载 | `mount \| grep nfs` |
