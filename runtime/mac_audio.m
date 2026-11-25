#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#include <stdio.h>

void *mac_audio_open(const char *path) {
    @autoreleasepool {
        NSString *name = [NSString stringWithUTF8String:path];
        NSURL *url = [NSURL fileURLWithPath:name];
        NSError *error = nil;
        AVAudioPlayer *player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&error];
        if (!player) {
            fprintf(stderr, "audio: cannot open %s: %s\n", path,
                    error ? [[error localizedDescription] UTF8String] : "unknown error");
            return NULL;
        }
        player.numberOfLoops = -1;
        player.volume = 0.31622777f;
        [player prepareToPlay];
        return (__bridge_retained void *)player;
    }
}

int mac_audio_play(void *handle) {
    @autoreleasepool { return handle && [(__bridge AVAudioPlayer *)handle play]; }
}

void mac_audio_pause(void *handle) {
    @autoreleasepool { if (handle) [(__bridge AVAudioPlayer *)handle pause]; }
}

void mac_audio_stop(void *handle) {
    @autoreleasepool {
        if (handle) {
            AVAudioPlayer *player = (__bridge AVAudioPlayer *)handle;
            [player stop];
            player.currentTime = 0;
        }
    }
}

void mac_audio_set_volume(void *handle, float volume) {
    @autoreleasepool {
        if (handle) {
            if (volume < 0.0f) volume = 0.0f;
            if (volume > 1.0f) volume = 1.0f;
            [(__bridge AVAudioPlayer *)handle setVolume:volume];
        }
    }
}

double mac_audio_duration(void *handle) {
    @autoreleasepool { return handle ? [(__bridge AVAudioPlayer *)handle duration] : 0.0; }
}

double mac_audio_position(void *handle) {
    @autoreleasepool { return handle ? [(__bridge AVAudioPlayer *)handle currentTime] : 0.0; }
}

void mac_audio_set_position(void *handle, double position) {
    @autoreleasepool {
        if (handle) {
            AVAudioPlayer *player = (__bridge AVAudioPlayer *)handle;
            if (position < 0.0) position = 0.0;
            if (position > player.duration) position = player.duration;
            player.currentTime = position;
        }
    }
}

void mac_audio_release(void *handle) {
    if (handle) {
        @autoreleasepool {
            AVAudioPlayer *player = (__bridge_transfer AVAudioPlayer *)handle;
            [player stop];
        }
    }
}
