# downsample-qt

这个项目目前包含两个核心类：

- `SpscDataRingBuffer<T>`
- `Downsampler`

## SpscDataRingBuffer

`SpscDataRingBuffer<T>` 是一个面向单生产者单消费者场景的无锁数据环形缓冲器。

适用场景：

- socket 线程持续接收数据
- 工作线程异步处理数据
- 写线程和读线程不能用悲观锁去锁住整块缓存
- 允许读线程跳过旧数据，只拿到最新一块完整发布的数据

特点：

- 单生产者、单消费者
- 无锁发布和读取
- 支持直接写入 `const T* + count`
- 支持直接写入 `QVector<T>`
- 在 Qt 5 下也支持直接写入 `QList<T>`
- 读取接口输出到 `QVector<T>`
- `T` 需要是 `trivially copyable`

示例：

```cpp
#include "spscdataringbuffer.h"

struct SamplePoint
{
    qint64 timestamp;
    float value;
};

SpscDataRingBuffer<SamplePoint> buffer(8, 1024);

QVector<SamplePoint> input(3);
input[0] = {1000, 1.0f};
input[1] = {1001, 2.0f};
input[2] = {1002, 3.0f};

if (buffer.push(input)) {
    QVector<SamplePoint> latest;
    if (buffer.tryReadLatest(latest)) {
        // latest 中是最近一次完整发布的数据块
    }
}
```

注意：

- 这个类不是 MPSC/MPMC 队列
- 它的目标是“最新数据优先”，不是“保证每块数据都被消费”
- 如果消费者处理速度跟不上，旧块会被跳过

## Downsampler

`Downsampler` 用于对大规模序列数据做降采样，返回的是原始数据索引，而不是直接复制后的值。

当前实现主要参考了 `tsdownsample` 库的算法设计和行为，包括：

- `MinMax`
- `M4`
- `LTTB`
- `MinMaxLTTB`

当前支持 4 种算法：

- `DownsampleAlgorithm::MinMax`
- `DownsampleAlgorithm::M4`
- `DownsampleAlgorithm::Lttb`
- `DownsampleAlgorithm::MinMaxLttb`

适用场景：

- 大量波形数据绘图
- 缩放视图后重新抽样显示
- 在不显示全部点的情况下尽量保留趋势和极值

特点：

- 输入使用 `QVector<double>`
- 输出使用 `QVector<qsizetype>`
- 支持只传 `y`
- 支持同时传 `x` 和 `y`
- 支持可选耗时统计输出参数

只传 `y` 的示例：

```cpp
#include "downsampler.h"

QVector<double> y;
for (int i = 0; i < 10000; ++i) {
    y.append(std::sin(i * 0.01));
}

qint64 elapsedNs = 0;
QVector<qsizetype> indices =
    Downsampler::downsample(y, 1000, DownsampleAlgorithm::MinMaxLttb, 4, &elapsedNs);

QVector<double> sampledY;
sampledY.reserve(indices.size());
for (qsizetype index : indices) {
    sampledY.append(y[index]);
}
```

传 `x` 和 `y` 的示例：

```cpp
#include "downsampler.h"

QVector<double> x;
QVector<double> y;

for (int i = 0; i < 10000; ++i) {
    x.append(i * 0.5);
    y.append(std::cos(i * 0.02));
}

QVector<qsizetype> indices =
    Downsampler::downsample(x, y, 800, DownsampleAlgorithm::Lttb);
```

注意：

- `MinMax` 的 `nOut` 应为偶数
- `M4` 的 `nOut` 应为 4 的倍数
- `Lttb` 的 `nOut` 至少应为 3
- `MinMaxLttb` 的 `minmaxRatio` 应大于 1
- 传入 `x` 时，`x` 必须升序并且和 `y` 长度一致

## 风险说明

本项目中的这部分代码由 GPT-5.4 生成或辅助生成。

在实际使用前，应该重点确认以下风险：

- 并发语义是否真的符合你的线程模型
- 降采样结果是否满足你的业务精度要求
- 大文件、极端值、空数据、异常输入下的边界行为
- Debug 和 Release 构建下的性能差异
- Qt 版本差异带来的接口和行为差异

这些代码不应在缺少人工审查、测试和压测的情况下直接用于高风险生产环境。
