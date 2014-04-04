package com.yiqingart.live;

import java.io.IOException;
import java.nio.ShortBuffer;

import android.hardware.Camera;
import android.hardware.Camera.PreviewCallback;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Bundle;
import android.os.PowerManager;
import android.app.Activity;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.util.Log;
import android.view.Display;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.Toast;

public class MainActivity extends Activity implements OnClickListener {
    private final static String CLASS_LABEL = "RecordActivity";
    private final static String LOG_TAG = CLASS_LABEL;
    private PowerManager.WakeLock mWakeLock;

    private String ffmpeg_link = "/mnt/sdcard/stream.flv";

    long startTime = 0;
    boolean recording = false;

   
    private boolean isPreviewOn = false;

    private int sampleAudioRateInHz = 8000;
    private int imageWidth = 320;
    private int imageHeight = 240;
    private int frameRate = 10;

    /* audio data getting thread */
    private AudioRecord audioRecord;
    private AudioRecordRunnable audioRecordRunnable;
    private Thread audioThread;
    volatile boolean runAudioThread = true;

    /* video data getting thread */
    private Camera cameraDevice;
    private CameraView cameraView;

    
    /* layout setting */
    private final int bg_screen_bx = 232;
    private final int bg_screen_by = 128;
    private final int bg_screen_width = 700;
    private final int bg_screen_height = 500;
    private final int bg_width = 1123;
    private final int bg_height = 715;
    private final int live_width = 640;
    private final int live_height = 480;
    private int screenWidth, screenHeight;
    private Button btnRecorderControl;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        setContentView(R.layout.main);
        
        setContentView(R.layout.main);

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE); 
        mWakeLock = pm.newWakeLock(PowerManager.SCREEN_BRIGHT_WAKE_LOCK, CLASS_LABEL); 
        mWakeLock.acquire(); 

        initLayout();
        initRecording();
    }
    
    @SuppressWarnings("deprecation")
    @Override
    protected void onResume() {
        super.onResume();

        if (mWakeLock == null) {
           PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
           mWakeLock = pm.newWakeLock(PowerManager.SCREEN_BRIGHT_WAKE_LOCK, CLASS_LABEL);
           mWakeLock.acquire();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();

        if (mWakeLock != null) {
            mWakeLock.release();
            mWakeLock = null;
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        recording = false;

        if (cameraView != null) {
            cameraView.stopPreview();
            cameraDevice.release();
            cameraDevice = null;
        }

        if (mWakeLock != null) {
            mWakeLock.release();
            mWakeLock = null;
        }
    }


    private void initLayout() {

        /* get size of screen */
        Display display = ((WindowManager) getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
        screenWidth = display.getWidth();
        screenHeight = display.getHeight();
        RelativeLayout.LayoutParams layoutParam = null; 
        LayoutInflater myInflate = null; 
        myInflate = (LayoutInflater) getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        RelativeLayout topLayout = new RelativeLayout(this);
        setContentView(topLayout);
        LinearLayout preViewLayout = (LinearLayout) myInflate.inflate(R.layout.main, null);
        layoutParam = new RelativeLayout.LayoutParams(screenWidth, screenHeight);
        topLayout.addView(preViewLayout, layoutParam);

        /* add control button: start and stop */
        btnRecorderControl = (Button) findViewById(R.id.recorder_control);
        btnRecorderControl.setText("开始直播");
        btnRecorderControl.setOnClickListener(this);

        /* add camera view */
        int display_width_d = (int) (1.0 * bg_screen_width * screenWidth / bg_width);
        int display_height_d = (int) (1.0 * bg_screen_height * screenHeight / bg_height);
        int prev_rw, prev_rh;
        if (1.0 * display_width_d / display_height_d > 1.0 * live_width / live_height) {
            prev_rh = display_height_d;
            prev_rw = (int) (1.0 * display_height_d * live_width / live_height);
        } else {
            prev_rw = display_width_d;
            prev_rh = (int) (1.0 * display_width_d * live_height / live_width);
        }
        layoutParam = new RelativeLayout.LayoutParams(prev_rw, prev_rh);
        layoutParam.topMargin = (int) (1.0 * bg_screen_by * screenHeight / bg_height);
        layoutParam.leftMargin = (int) (1.0 * bg_screen_bx * screenWidth / bg_width);

        cameraDevice = Camera.open();
        Log.i(LOG_TAG, "cameara open");
        cameraView = new CameraView(this, cameraDevice);
        topLayout.addView(cameraView, layoutParam);
        Log.i(LOG_TAG, "cameara preview start: OK");
    }

    private void initRecording() {

        Log.w(LOG_TAG,"init recorder");

        Log.i(LOG_TAG, "ffmpeg_url: " + ffmpeg_link);
        initRecorder();

        Log.i(LOG_TAG, "recorder initialize success");

        audioRecordRunnable = new AudioRecordRunnable();
        audioThread = new Thread(audioRecordRunnable);
    }
    private void createAudioRecord() { 
        int mBufferSize;

       // TODO(billhoo) should try user's specific combinations first, if it's invalid, then do for loop to get a 
       // available combination instead. 

       // Find best/compatible AudioRecord 
       // If all combinations are invalid, throw IllegalStateException 
       for (int sampleRate : new int[] { 8000, 11025, 16000, 22050, 32000, 
               44100, 47250, 48000 }) { 
           for (short audioFormat : new short[] { 
                   AudioFormat.ENCODING_PCM_16BIT, 
                   AudioFormat.ENCODING_PCM_8BIT }) { 
               for (short channelConfig : new short[] { 
                       AudioFormat.CHANNEL_IN_MONO, 
                       AudioFormat.CHANNEL_IN_STEREO, 
                       AudioFormat.CHANNEL_CONFIGURATION_MONO, 
                       AudioFormat.CHANNEL_CONFIGURATION_STEREO }) { 

                   // Try to initialize 
                   try { 
                       mBufferSize = AudioRecord.getMinBufferSize(sampleRate, 
                               channelConfig, audioFormat); 

                       if (mBufferSize < 0) { 
                           continue; 
                       } 
                       Toast.makeText(MainActivity.this, ""+sampleRate+":"+audioFormat+":"+channelConfig,
                               Toast.LENGTH_SHORT).show();
                       
                       Log.d(LOG_TAG, "sampleRate:"+sampleRate+" audioFormat:"+audioFormat+" channelConfig:"+channelConfig);

                       
                   } catch (Exception e) { 
                       // Do nothing 
                   } 
               } 
           } 
       } 

       // ADDED(billhoo) all combinations are failed on this device. 
       //throw new IllegalStateException( 
        //       "getInstance() failed : no suitable audio configurations on this device."); 
   } 
   //---------------------------------------------
   // audio thread, gets and encodes audio data
   //---------------------------------------------
   class AudioRecordRunnable implements Runnable {
       short[] pending = new short[0];
       private void writeAudioSamples(short[] buffer, int bufferReadResult) {

           int pendingArrLength = pending.length;
           short[] newArray = new short[bufferReadResult + pendingArrLength];

           System.arraycopy(pending, 0, newArray, 0, pendingArrLength);
           System.arraycopy(buffer, 0, newArray, pendingArrLength,bufferReadResult);

           int len = newArray.length;
           int q = Math.abs(len / 1024);
           int r = len % 1024;

           ShortBuffer shortBuffer = ShortBuffer.wrap(newArray);
           for (int i = 0; i < q && recording; i++) {
               short dst[] = new short[1024];
               shortBuffer.get(dst);
               SupplyAudioSamples(dst, dst.length);
           }
           pending = new short[r];
           shortBuffer.get(pending);
       }
       @Override
       public void run() {
           android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_URGENT_AUDIO);

           // Audio
           int bufferSize;
           short[] audioData;
           int bufferReadResult;

           bufferSize = AudioRecord.getMinBufferSize(sampleAudioRateInHz, 
                   AudioFormat.CHANNEL_IN_DEFAULT, AudioFormat.ENCODING_PCM_16BIT);
           Log.d(LOG_TAG, "bufferSize = "+bufferSize);
           audioRecord = new AudioRecord(MediaRecorder.AudioSource.MIC, sampleAudioRateInHz, 
                   AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, bufferSize);

           audioData = new short[bufferSize];

           Log.d(LOG_TAG, "audioRecord.startRecording()");
           audioRecord.startRecording();

           /* ffmpeg_audio encoding loop */
           while (runAudioThread) {
               //Log.v(LOG_TAG,"recording? " + recording);
               bufferReadResult = audioRecord.read(audioData, 0, audioData.length);
               if (bufferReadResult > 0) {
                   Log.v(LOG_TAG,"bufferReadResult: " + bufferReadResult);
                   // If "recording" isn't true when start this thread, it never get's set according to this if statement...!!!
                   // Why?  Good question...
                   if (recording) {
                       try {
                           writeAudioSamples(audioData, bufferReadResult);
                           //Log.v(LOG_TAG,"recording " + 1024*i + " to " + 1024*i+1024);
                       } catch (Exception e) {
                           Log.v(LOG_TAG,e.getMessage());
                           e.printStackTrace();
                       }
                   }
               }
           }
           Log.v(LOG_TAG,"AudioThread Finished, release audioRecord");

           /* encoding finish, release recorder */
           if (audioRecord != null) {
               audioRecord.stop();
               audioRecord.release();
               audioRecord = null;
               Log.v(LOG_TAG,"audioRecord released");
           }
       }
   }
    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.main, menu);
        return true;
    }

    @Override
    public void onClick(View arg0) {
        if (!recording) {
            startRecording();
            Log.w(LOG_TAG, "Start Button Pushed");
            btnRecorderControl.setText("停止");
        } else {
            // This will trigger the audio recording loop to stop and then set isRecorderStart = false;
            stopRecording();
            Log.w(LOG_TAG, "Stop Button Pushed");
            btnRecorderControl.setText("开始直播");
        }
    }
    public void startRecording() {

        try {
            startRecorder();
            
            startTime = System.currentTimeMillis();
            recording = true;
            //createAudioRecord();
            audioThread.start();

        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public void stopRecording() {

        runAudioThread = false;

        if (recording) {
            recording = false;
            Log.v(LOG_TAG,"Finishing recording, calling stop and release on recorder");
            try {
                stopRecorder();
            } catch (Exception e) {
                e.printStackTrace();
            }

        }
    }
  

    //---------------------------------------------
    // camera thread, gets and encodes video data
    //---------------------------------------------
    class CameraView extends SurfaceView implements SurfaceHolder.Callback, PreviewCallback {

        private SurfaceHolder mHolder;
        private Camera mCamera;

        public CameraView(Context context, Camera camera) {
            super(context);
            Log.w("camera","camera view");
            mCamera = camera;
            mHolder = getHolder();
            mHolder.addCallback(CameraView.this);
            mHolder.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);
            mCamera.setPreviewCallback(CameraView.this);
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
            try {
                stopPreview();
                mCamera.setPreviewDisplay(holder);
            } catch (IOException exception) {
                mCamera.release();
                mCamera = null;
            }
        }

        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            Log.v(LOG_TAG,"Setting imageWidth: " + imageWidth + " imageHeight: " + imageHeight + " frameRate: " + frameRate);
            Camera.Parameters camParams = mCamera.getParameters();
            camParams.setPreviewSize(imageWidth, imageHeight);
    
            Log.v(LOG_TAG,"Preview Framerate: " + camParams.getPreviewFrameRate());
    
            camParams.setPreviewFrameRate(frameRate);
            mCamera.setParameters(camParams);
            startPreview();
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            try {
                mHolder.addCallback(null);
                mCamera.setPreviewCallback(null);
            } catch (RuntimeException e) {
                // The camera has probably just been released, ignore.
            }
        }

        public void startPreview() {
            if (!isPreviewOn && mCamera != null) {
                isPreviewOn = true;
                mCamera.startPreview();
            }
        }

        public void stopPreview() {
            if (isPreviewOn && mCamera != null) {
                isPreviewOn = false;
                mCamera.stopPreview();
            }
        }

        @Override
        public void onPreviewFrame(byte[] data, Camera camera) {
            /* get video data */
            if (recording) {
                //Log.v(LOG_TAG,"Writing Frame:"+data.length);
                try {
                    long t = 1000 * (System.currentTimeMillis() - startTime);
                    SupplyVideoFrame(data, data.length, t);
                } catch (Exception e) {
                    Log.v(LOG_TAG,e.getMessage());
                    e.printStackTrace();
                }
            }
        }
    }
    
    public native int  initRecorder();
    public native int  startRecorder();
    public native int  stopRecorder();
    public native int  SupplyAudioSamples(short[] buffer, long len);
    public native int  SupplyVideoFrame(byte[] buffer, long len, long timestamp);
    
    static {
        System.loadLibrary("recorder");
    }
}
