# Dirwatcher

간단한 디렉터리 감시 라이브러리입니다.

## Patch note

- `v0.1.0` - Win32용 구현 완성

## Usage

```c
// 1. 타겟 디렉터리 열기
dirwatcher_target_t target = dirwatcher_open_target("PATH/TO/DIR");

// 2. 이벤트 콜백 설정
dirwatcher_set_target_callback(target, my_callback);

// 3. 감시 시작
dirwatcher_start_watch_target(target);

// 4. 감시 일시 중지
dirwatcher_stop_watch_target(target);

// 5. 타겟 닫기
dirwatcher_close_target(target);
```

## License

*CC0 1.0 유니버설*

- 이 증서와 함께 저작물을 공개한 사람은  
  저작권법을 포함하여 관련 및 인접 권리 전반에 대해,  
  법이 허용하는 최대 범위 내에서 전 세계적으로 해당 저작물에 대한  
  모든 권리를 포기함으로써 이 저작물을 퍼블릭 도메인에 헌정하였습니다.  
  귀하는 허락을 요청하지 않고도,  
  상업적 목적을 포함하여 이 저작물을 복사, 수정, 배포 및 공연할 수 있습니다.  
  이 저작물은 어떠한 보증도 없이 “있는 그대로” 제공됩니다.
