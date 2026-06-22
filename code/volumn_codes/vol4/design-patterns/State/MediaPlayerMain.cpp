#include "MediaPlayer.h"

#include <iostream>

// 演示用法:你只跟 MediaPlayer 这层壳打交道,内部状态怎么切完全不用管。
// 驱动按 play -> pause -> play -> stop -> pause 的顺序推一遍状态机,
// 正好走完三条有效转换(Stopped->Playing / Playing->Paused / Paused->Playing /
// Playing->Stopped)和一条「该被忽略」的调用(Stopped 状态下 pause())。
int main() {
    MediaPlayer player(std::make_shared<StoppedState>());

    player.play();  // Stopped -> Playing
    player.pause(); // Playing  -> Paused
    player.play();  // Paused   -> Playing
    player.stop();  // Playing  -> Stopped
    player.pause(); // Stopped: pause() ignored

    return 0;
}
