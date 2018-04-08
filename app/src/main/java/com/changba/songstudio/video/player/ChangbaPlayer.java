package com.changba.songstudio.video.player;

import com.changba.songstudio.video.decoder.MediaCodecDecoderLifeCycle;
import com.changba.songstudio.video.player.OnInitializedCallback.OnInitialStatus;

import android.util.Log;
import android.view.Surface;

public class ChangbaPlayer extends MediaCodecDecoderLifeCycle {

	public void init(final String srcFilenameParam, final Surface surface, final int width, final int height,
			final OnInitializedCallback onInitializedCallback) {
		this.init(srcFilenameParam, new int[] { -1, -1, -1 }, -1, 0.5f, 1.0f, surface, width, height,
				onInitializedCallback);
	}

	public void init(final String srcFilenameParam, final int[] max_analyze_duration, final int probesize,
			final float minBufferedDuration, final float maxBufferedDuration, final Surface surface, final int width,
			final int height, final OnInitializedCallback onInitializedCallback) {
		initializedCallback = onInitializedCallback;

		prepare(srcFilenameParam, max_analyze_duration, max_analyze_duration.length, probesize, true, minBufferedDuration,
				maxBufferedDuration, width, height, surface);
	}

	/* OnInitializedCallback */
	private OnInitializedCallback initializedCallback;

	public void onInitializedFromNative(boolean initCode) {
		if (initializedCallback != null){
			OnInitialStatus onInitialStatus = initCode ? OnInitialStatus.CONNECT_SUCESS : OnInitialStatus.CONNECT_FAILED;
			initializedCallback.onInitialized(onInitialStatus);
		}
	}


	public native void onSurfaceCreated(final Surface surface);

	public native void onSurfaceDestroyed(final Surface surface);

	/**
	 * 初始化
	 *
	 * @param srcFilenameParam
	 *            文件路径或者直播地址
	 * @return 是否正确初始化
	 */
	public native boolean prepare(String srcFilenameParam, int[] max_analyze_durations, int size, int probesize, boolean fpsProbesizeConfigured,
			float minBufferedDuration, float maxBufferedDuration, int width, int height, Surface surface);

	/**
	 * 暂停播放
	 */
	public native void pause();

	/**
	 * 继续播放
	 */
	public native void play();

	/**
	 * 停止播放
	 */
	public void stopPlay() {
		new Thread() {
			public void run() {
				ChangbaPlayer.this.stop();
			}
		}.start();
	}

	public native void stop();

	/**
	 * 获得缓冲进度 返回秒数（单位秒 但是小数点后有3位 精确到毫秒）
	 */
	public native float getBufferedProgress();

	/**
	 * 获得播放进度（单位秒 但是小数点后有3位 精确到毫秒）
	 */
	public native float getPlayProgress();

	/**
	 * 跳转到某一个位置
	 */
	public native void seekToPosition(float position);
	
	/**
	 * 只做seek操作，seek到指定位置
	 */
	public native void seekCurrent(float position);
	
	public native void beforeSeekCurrent();
	public native void afterSeekCurrent();

	public void showLoadingDialog() {
		Log.i("problem", "showLoadingDialog...");
	}

	public void hideLoadingDialog() {
		Log.i("problem", "hideLoadingDialog...");
	}

	public void onCompletion() {
		Log.i("problem", "onCompletion...");
	}

	public void videoDecodeException() {
		Log.i("problem", "videoDecodeException...");
	}

	public void viewStreamMetaCallback(int width, int height, float duration) {
		Log.i("problem", "width is : " + width + ";height is : " + height + ";duration is : " + duration);
	}

	public native void resetRenderSize(int left, int top, int width, int height);
}
