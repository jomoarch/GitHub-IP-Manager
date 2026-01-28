# GHIP
这是送给中国 Linux 用户的一个用 C++ 编写的交互式 GitHub IP 获取工具，在终端中运行，支持测试当前较快的 GitHub IP 列表并进行排序，便于用户挑选适合自己的 GitHub IP 。

目前这个项目存在很大的漏洞，正在修改中，请慎重使用。

## 使用过程
先用 git 将其下载到你的 Linux 上
```bash
git clone https://github.com/jomoarch/GHIP.git
```

然后进入项目并运行 `build.sh` 脚本
```bash
cd GHIP
chmod +x build.sh
./build.sh
```

它会生成一个 `build\` 目录，运行里面的 `GHIP` 程序即可
```bash
./build/GHIP
```