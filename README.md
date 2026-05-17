# house-price

一个用 C 语言实现的简单房价回归分析程序。

## 功能

- 读取 `housing-price.txt` 数据集
- 计算 13 个特征与房价 `MEDV` 的相关系数
- 自动选择相关性绝对值最高的 4 个特征
- 使用多元线性回归拟合并输出 RMSE

## 编译

在 Windows 下可以使用 MinGW GCC：

```bash
gcc -fdiagnostics-color=always -g Untitled-1.c -o Untitled-1.exe -lm
```

## 运行

默认读取当前目录下的 `housing-price.txt`：

```bash
Untitled-1.exe
```

也可以手动指定数据文件路径：

```bash
Untitled-1.exe housing-price.txt
```

## 说明

- 程序输出已针对 Windows 控制台做了中文编码处理
- 如果你在其他环境编译，可能还需要确保终端使用 UTF-8
