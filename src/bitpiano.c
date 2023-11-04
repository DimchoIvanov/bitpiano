#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>
#include <sys/types.h>
#include <math.h>

//#include "wav_parser.h"

snd_pcm_t *playback_handle;
//int32_t buf[4096];
//int32_t buf2[4096];
int32_t buf[8192];
int32_t buf2[8192];
uint8_t bbuf[32768];
uint8_t bbuf2[32768];
uint8_t bbuf3[32768];
uint32_t bbuf3_size = 0;

static volatile int exit_flag = 0;

typedef union frame_u {
    uint8_t raw[24];
    struct {
        uint32_t l11;
        uint16_t l12;
        uint32_t r11;
        uint16_t r12;
        uint32_t l21;
        uint16_t l22;
        uint32_t r21;
        uint16_t r22;
    } v1;
} frame_t;

static int interleaved = 0;
static int mmap = 0;

int
playback_callback (void* bf, snd_pcm_sframes_t nframes)
{
    int err;
    static int counter = 0;
    //printf ("playback callback called with %ld frames cnt[%d]\n", nframes, ++counter);

    //////////////////////////////////
    if (interleaved) {
        if (mmap) {
            if ((err = snd_pcm_mmap_writei(playback_handle, bf, nframes)) < 0) {
                fprintf (stderr, "mmap_writei failed [%d](%s)\n", err, snd_strerror (err));
            }
        }
        else {
            if ((err = snd_pcm_writei (playback_handle, bf, nframes)) < 0) {
                fprintf (stderr, "writei failed [%d](%s)\n", err, snd_strerror (err));
            }
        }
    }
    //////////////////////////////////

    return err;
}

size_t fmt_size(snd_pcm_format_t fmt)
{
    switch (fmt)
    {
        case SND_PCM_FORMAT_S32_LE:
            return 4;
            break;

        case SND_PCM_FORMAT_S24_LE:
        case SND_PCM_FORMAT_S24_3LE:
            return 3;
            break;

        case SND_PCM_FORMAT_S16_LE:
        default:
            return 2;
            break;
    }
}

void signal_handler(int sig)
{
    exit_flag = 1;
    fprintf (stderr, "\n signal_handler[%d]. exiting ...\n", sig);
}


typedef struct smpl_buff_s {
    // "fmt" sub-chunk properties
    uint16_t        AudioFormat;    // Audio format 1=PCM,6=mulaw,7=alaw,     257=IBM Mu-Law, 258=IBM A-Law, 259=ADPCM
    uint16_t        NumOfChan;      // Number of channels 1=Mono 2=Sterio
    uint32_t        SamplesPerSec;  // Sampling Frequency in Hz
    uint32_t        bytesPerSec;    // bytes per second
    uint16_t        blockAlign;     // 2=16-bit mono, 4=16-bit stereo
    uint16_t        bitsPerSample;  // Number of bits per sample
    uint16_t        numFrms;
    uint16_t        numSmpl;
} smpl_buff_t;

int
main (int argc, char *argv[])
{

    signal(SIGINT, signal_handler);

    snd_pcm_hw_params_t *hw_params;
    snd_pcm_sw_params_t *sw_params;
    snd_pcm_sframes_t frames_to_deliver;
    int nfds;
    int err;
    struct pollfd *pfds;
    int fd = -1;
    FILE* file = NULL;
  //unsigned int rate = 44100;
    unsigned int rate = 48000;
  //unsigned int rate = 96000;
    unsigned int chan = 2;
    unsigned int frequency = 300;
    //unsigned int num_frms = 0;
    unsigned int num_smpls = 0;
    snd_pcm_format_t format =  SND_PCM_FORMAT_S16; // SND_PCM_FORMAT_S24_3LE ; // SND_PCM_FORMAT_U8 ; //SND_PCM_FORMAT_S32 ;// SND_PCM_FORMAT_S16; // SND_PCM_FORMAT_S32
    snd_pcm_format_t devfmt = SND_PCM_FORMAT_S32 ; // SND_PCM_FORMAT_UNKNOWN;
    snd_pcm_access_t devaccss = SND_PCM_ACCESS_MMAP_INTERLEAVED;
    smpl_buff_t format_data;
    int ready_to_break = 0;

    if ((argc >= 1) && (argv[1] != NULL))
    {
        //        file = fopen(argv[2], "rb");
        //        if (file == NULL) {
        //            fprintf (stderr, "cannot open file <%s> (%s)\n", argv[2], strerror (err));
        //        }
        //        else
        {
            //parse_wave_header1(file, &format_data);
            //exit_flag = 1;

            format_data.bitsPerSample = fmt_size(format)*8;
            format_data.NumOfChan = chan;
            format_data.SamplesPerSec = rate ; // * format_data.bitsPerSample/8;
            format_data.blockAlign = format_data.NumOfChan * format_data.bitsPerSample/8;

            format_data.numSmpl = format_data.SamplesPerSec / frequency ;
            format_data.numFrms  = format_data.numSmpl;
            bbuf3_size = format_data.numSmpl * format_data.blockAlign;

            fprintf(stderr, "format_data.SamplesPerSec[%u] format_data.blockAlign[%u] format_data.numFrms[%u] format_data.numSmpl[%u] bbuf3_size[%u]\n",
                    format_data.SamplesPerSec, format_data.blockAlign, format_data.numFrms, format_data.numSmpl, bbuf3_size);

            double phase = 0;
            char* q;
            int16_t copy_i = 0;
            char* p = (char*)&bbuf3[0];
            for (int i = 0; ((i < format_data.numSmpl) && (phase <= 6.283)); i++)
            {
                phase = (6.28 * (double)i) / ((double)format_data.numSmpl);
                double val_d = sin(phase);
                val_d = round((val_d * 0xFFFF));
                double val_h = round((val_d / 2));
                int16_t val_i = ((int16_t)(val_d/2)) & 0xFFFF;

                memcpy(p, &val_i, fmt_size(format));
                p += fmt_size(format);

                memcpy(p, &val_i, fmt_size(format));
                p += fmt_size(format);

                fprintf(stderr, "-i[%d] ph[%01.3f] vd[%01.1f] vh[%01.1f] vi[0x%02x] [%ld]\n",
                        i, phase, val_d, val_h, val_i, (p - ((char*)&bbuf3[0])));
            }
            fprintf(stderr, "bytes_writen[%ld]\n", p - ((char*)&bbuf3[0]));
        }
    }

    static int open_mode = 0 ;//| SND_PCM_NONBLOCK;
    static snd_output_t *logstd;

    if ((err = snd_output_stdio_attach(&logstd, stderr, 0)) < 0) {
        fprintf (stderr, "cannot attach stdio [%d] (%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_open (&playback_handle, argv[1], SND_PCM_STREAM_PLAYBACK, open_mode)) < 0) {
        fprintf (stderr, "cannot open audio device %s (%s)\n", argv[1], snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
        fprintf (stderr, "cannot allocate hardware parameter structure [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params_any (playback_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot initialize hardware parameter structure [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    //snd_pcm_dump_hw_setup(pcm, out)
    snd_pcm_hw_params_dump(hw_params, logstd);

    unsigned int supp_rate = -1;
    unsigned int supp_rate_min = -1;
    unsigned int supp_rate_max = -1;

    if ((err = snd_pcm_hw_params_get_rate(hw_params, &supp_rate, 0)) < 0) {
        fprintf (stderr, "cannot get rate [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get rate [%u]\n", supp_rate);
    }

    if ((err = snd_pcm_hw_params_get_rate_min(hw_params, &supp_rate_min, 0)) < 0) {
        fprintf (stderr, "cannot get rate min [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get rate min[%u]\n", supp_rate_min);
    }

    if ((err = snd_pcm_hw_params_get_rate_max(hw_params, &supp_rate_max, 0)) < 0) {
        fprintf (stderr, "cannot get rate max [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get rate max[%u]\n", supp_rate_max);
    }

    if ((err = snd_pcm_hw_params_test_rate(playback_handle, hw_params, rate, 0)) < 0) {
        fprintf (stderr, "rate [%d] test failed [%d](%s)\n", rate, err, snd_strerror (err));

        if (supp_rate > 0) {
            rate = supp_rate;
        }
    }
    else {
        fprintf (stderr, "Rate [%u] tested successfully.\n", rate);
    }

    unsigned int chan_min=0, chan_max=0;

    if ((err = snd_pcm_hw_params_get_channels(hw_params, &chan)) < 0) {
        fprintf (stderr, "cannot get channels number [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "get channels [%u]\n", chan);
    }

    if ((err = snd_pcm_hw_params_test_channels(playback_handle, hw_params, chan)) < 0) {
        fprintf (stderr, "channels [%d] test failed [%d](%s)\n", chan, err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Chan number [%u] tested successfully.\n", chan);
    }

    if ((err = snd_pcm_hw_params_get_channels_min(hw_params, &chan_min)) < 0) {
        fprintf (stderr, "cannot get chan_min number [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get chan min[%u]\n", chan_min);
    }

    if ((err = snd_pcm_hw_params_get_channels_max(hw_params, &chan_max)) < 0) {
        fprintf (stderr, "cannot get chan_max number [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get chan max[%u]\n", chan_max);
    }

    if ((err = snd_pcm_hw_params_test_format(playback_handle, hw_params, format)) < 0) {
        fprintf (stderr, "Format [%d] test failed [%d](%s)\n", format, err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Format [%d] tested successfully.\n", format);
    }

    //devfmt = format;
    if ((err = snd_pcm_hw_params_get_format(hw_params, &devfmt)) < 0) {
        fprintf (stderr, "cannot get format [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get format[%d]\n", (int)devfmt);
    }

    if ((err = snd_pcm_hw_params_get_access(hw_params, &devaccss)) < 0) {
        fprintf (stderr, "cannot get access type [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get access type[%d]\n", (int)devaccss);
    }

    //int snd_pcm_hw_params_get_format(const snd_pcm_hw_params_t *params, snd_pcm_format_t *val);


    // SND_PCM_ACCESS_MMAP_INTERLEAVED
    if ((err = snd_pcm_hw_params_test_access (playback_handle, hw_params, SND_PCM_ACCESS_RW_NONINTERLEAVED)) == 0) {
        devaccss = SND_PCM_ACCESS_RW_NONINTERLEAVED;
        interleaved = 0;
        mmap = 0;
        fprintf (stderr, "Access [%d] tested successfully.\n", devaccss);
    }
    else {
        if ((err = snd_pcm_hw_params_test_access (playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) == 0) {
            devaccss = SND_PCM_ACCESS_RW_INTERLEAVED;
            interleaved = 1;
            mmap = 0;
            fprintf (stderr, "Access [%d] tested successfully.\n", devaccss);
        }
        else {
            if ((err = snd_pcm_hw_params_test_access (playback_handle, hw_params, SND_PCM_ACCESS_MMAP_INTERLEAVED)) == 0) {
                devaccss = SND_PCM_ACCESS_MMAP_INTERLEAVED;
                interleaved = 1;
                mmap = 1;
                fprintf (stderr, "Access [%d] tested successfully.\n", devaccss);
            }
            else {
                if ((err = snd_pcm_hw_params_test_access (playback_handle, hw_params, SND_PCM_ACCESS_MMAP_NONINTERLEAVED)) == 0) {
                    devaccss = SND_PCM_ACCESS_MMAP_NONINTERLEAVED;
                    interleaved = 0;
                    mmap = 1;
                    fprintf (stderr, "Access [%d] tested successfully.\n", devaccss);
                }
            }
        }
    }
    ///////////

    if ((err = snd_pcm_hw_params_get_sbits(hw_params)) < 0) {
        fprintf (stderr, "cannot get hw_sbits [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get sbits[%d]\n", err);
    }

    if ((err = snd_pcm_hw_params_set_access (playback_handle, hw_params, devaccss)) < 0) {
        fprintf (stderr, "cannot set access type [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params_set_format (playback_handle, hw_params, /*format*/ devfmt)) < 0) {
        fprintf (stderr, "cannot set sample format [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params_set_rate_near (playback_handle, hw_params, &rate, 0)) < 0) {
        fprintf (stderr, "cannot set sample rate [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params_set_channels (playback_handle, hw_params, chan)) < 0) {
        fprintf (stderr, "cannot set channel count [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params (playback_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot set parameters [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    snd_pcm_hw_params_free (hw_params);

    /* tell ALSA to wake us up whenever 4096 or more frames
       of playback data can be delivered. Also, tell
       ALSA that we'll start the device ourselves.
    */

#define FRMLIMIT    1024 //4096 // 4096


    if ((err = snd_pcm_sw_params_malloc (&sw_params)) < 0) {
        fprintf (stderr, "cannot allocate software parameters structure [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_sw_params_current (playback_handle, sw_params)) < 0) {
        fprintf (stderr, "cannot initialize software parameters structure [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_sw_params_set_avail_min (playback_handle, sw_params, FRMLIMIT)) < 0) {
        fprintf (stderr, "cannot set minimum available count [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_sw_params_set_start_threshold (playback_handle, sw_params, FRMLIMIT)) < 0) {
        fprintf (stderr, "cannot set start mode [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_sw_params (playback_handle, sw_params)) < 0) {
        fprintf (stderr, "cannot set software parameters [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    /* the interface will interrupt the kernel every 4096 frames, and ALSA
       will wake up this program very soon after that.
    */

    if ((err = snd_pcm_prepare (playback_handle)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    while (!exit_flag) {

        frame_t f1;
        f1.raw[0] = 0x11;
        f1.raw[1] = 0x22;
        f1.raw[2] = 0x33;
        f1.raw[3] = 0x44;
        f1.raw[4] = 0x55;
        f1.raw[5] = 0x66;
        f1.raw[6] = 0x77;
        f1.raw[7] = 0x88;
        f1.raw[8] = 0x99;
        f1.raw[9] = 0xaa;
        f1.raw[11] = 0xbb;
        f1.raw[12] = 0xcc;
        f1.raw[13] = 0xdd;
        f1.raw[14] = 0xee;
        f1.raw[15] = 0xff;
        f1.raw[16] = 0xa1;
        f1.raw[17] = 0xb1;
        f1.raw[18] = 0xc1;
        f1.raw[19] = 0xd1;
        f1.raw[20] = 0xe1;
        f1.raw[21] = 0xf1;
        f1.raw[22] = 0xa2;
        f1.raw[23] = 0xb2;

        /* wait till the interface is ready for data, or 1 second
           has elapsed.
        */

        if ((err = snd_pcm_wait (playback_handle, 1000)) < 0) {
                fprintf (stderr, "poll failed (%s)\n", strerror (errno));
                break;
        }

        /* find out how much space is available for playback data */

        if ((frames_to_deliver = snd_pcm_avail_update (playback_handle)) < 0) {
            if (frames_to_deliver == -EPIPE) {
                fprintf (stderr, "an xrun occured\n");
                break;
            }
            else {
                fprintf (stderr, "unknown ALSA avail update return value (%ld)\n", frames_to_deliver);
                break;
            }
        }
        else
        {
            // frames_to_deliver = frames_to_deliver > FRMLIMIT ? FRMLIMIT : frames_to_deliver;
            //size_t frames_read = fread((void*)bbuf2, format_data.blockAlign, (size_t)frames_to_deliver, file);

            //frames_to_deliver = frames_to_deliver > FRMLIMIT ? FRMLIMIT : frames_to_deliver;
            frames_to_deliver = frames_to_deliver > format_data.numFrms ? format_data.numFrms : frames_to_deliver;

            memcpy((void*)bbuf2, (void*)bbuf3, bbuf3_size);

            // frames_to_deliver = format_data.numFrms;

            if (format_data.numFrms == 0)
            {
                // Apply chunk of silence beyond the end of file content.
                // This is a workaround.
                // This way we are going to hide the repetition of last data chunk when alsa goes out of data.
//                if (!ready_to_break) {
//                    snd_pcm_format_set_silence(devfmt, bbuf2, FRMLIMIT);
//                    ready_to_break = 1;
//                    fprintf (stderr, "fread [%lu] - EOL?\n", frames_read);
//                }
//                else
//                    break;
            }
        }

        //memcpy(bbuf,bbuf2,(frames_to_deliver * format_data.blockAlign));
        uint16_t targetBlockAlign = format_data.NumOfChan * fmt_size(devfmt);

        switch (format_data.bitsPerSample)
        {
        case 32:
        {
            if (fmt_size(devfmt)*8 == 32)
            {
                // Copy data
                // 32-to-32
                memcpy(bbuf,bbuf2,(frames_to_deliver * format_data.blockAlign));
            }
            else if (fmt_size(devfmt)*8 == 24)
            {
                // Convert data
                // 32-to-24
                int j=0;
                for (int i=0; i < (size_t)(frames_to_deliver * format_data.blockAlign); i+=8)
                {
                    bbuf[j]   = bbuf2[i+1];  //
                    bbuf[j+1] = bbuf2[i+2];  // msb?
                    bbuf[j+2] = bbuf2[i+3];  // lsb?
                    bbuf[j+3] = bbuf2[i+5];  //
                    bbuf[j+4] = bbuf2[i+6];  // right
                    bbuf[j+5] = bbuf2[i+7];  //
                    j+=6;
                }
            }
            // Copy data
            // 32-to-32
            memcpy(bbuf,bbuf2,(frames_to_deliver * format_data.blockAlign));

        } break;

        case 24:
        {
            memcpy(bbuf,bbuf2,(frames_to_deliver * format_data.blockAlign));
//            memcpy(buf2, f1.raw, sizeof(f1.raw));
//
//            int j=0;
//            int max_i = frames_to_deliver * format_data.blockAlign / 4;
//            // 24-to-32 ?
//            for (int i=0; i < (size_t)max_i; i+=3)
//            {
//                buf[j]   =  (buf2[i] << 8);
//                buf[j+1] = ((buf2[i] >> 16) & 0x0000FF00) | ((buf2[i+1] << 16) & 0xFFFF0000);
//                buf[j+2] = 0;//(buf2[i+1] << 16) | ((buf2[i+2] >> 16) & 0x0000FF00);
//                buf[j+3] = 0;//(buf2[i+2] ) & 0xFFFFFF00;  //; << 8// & 0xFFFFFF00;
//                j+=4;
//            }
//            for (int i=0; i < (size_t)max_i; i+=3)
//            {
//                buf[j]   =  buf2[i] & 0xFFFFFF00;
//                buf[j+1] = 0;//(buf2[i]   << 24) | ((buf2[i+1] >> 8) & 0x00FFFF00);
//                buf[j+2] = (buf2[i+1] << 16) | ((buf2[i+2] >> 16) & 0x0000FF00);
//                buf[j+3] = 0;// buf2[i+2] << 8;
//                j+=4;
//            }
        } break;

        case 16:
        {
            if (fmt_size(devfmt)*8 == 16)
            {
                memcpy(bbuf, bbuf2, (frames_to_deliver * format_data.blockAlign));
            }
            else if (fmt_size(devfmt)*8 == 24) {
                // 16-to-24
                int j=0;
                for (int i=0; i < (size_t)(frames_to_deliver * format_data.blockAlign); i+=4)
                {
                    bbuf[j]   = 0;           // ??bbuf2[i];
                    bbuf[j+1] = bbuf2[i];    // msb?
                    bbuf[j+2] = bbuf2[i+1];  // lsb?
                    bbuf[j+3] = 0;           // ??bbuf2[i+2];
                    bbuf[j+4] = bbuf2[i+2];  // right
                    bbuf[j+5] = bbuf2[i+3];  //
                    j+=6;
                }
            }
            else
            {
                // Convert data
                // 16-to-32
                int j=0;
                for (int i=0; i < (size_t)(frames_to_deliver * format_data.blockAlign); i+=4)
                {
    //                buf[i*2]   = buf2[i] & 0xFFFF0000;//buf2[i];//(buf2[i] << 16);
    //                buf[i*2+1] = (buf2[i] << 16) & 0xFFFF0000;
                    bbuf[j]   = 0;//--//bbuf2[i];
                    bbuf[j+1] = 0;//--//bbuf2[i];
                    bbuf[j+2] = bbuf2[i];    // msb?
                    bbuf[j+3] = bbuf2[i+1];  //0;// lsb?
                    bbuf[j+4] = 0;//--//bbuf2[i+2];
                    bbuf[j+5] = 0;//--//bbuf2[i+2];
                    bbuf[j+6] = bbuf2[i+2];    // right
                    bbuf[j+7] = bbuf2[i+3]; //0; //
                    j+=8;
                }
            }
        } break;

        default:
            fprintf (stderr, "Unsupported bit map!\n");
            break;
        }

        static bool print_once = true;

        if (print_once)
        {
            fprintf (stderr, "BB:uf2\n");

            int width = fmt_size(devfmt)*chan; // format_data.blockAlign;
            for (int i=0; i < 100; i+=width)
            {
                char strbuf[128] = {0};
                int left = 128;
                char* pp = strbuf;
                for (int j=0; j < width; j++)
                {
                    int res = snprintf(pp, left,"[%02x]", bbuf[i+j]);
                    if (res > 0)
                    {
                        pp += res;
                        left -= res;
                    }
                    if (j==(width/2-1))
                    {
                        res = snprintf(pp, left,"%c",':');
                        if (res > 0)
                        {
                            pp += res;
                            left -= res;
                        }
                    }
                }
                fprintf (stderr, "%s\n", strbuf);
            }
            fprintf (stderr, "-----:\n");

            print_once = false;
        }

        /* deliver the data */

        if (playback_callback(bbuf, frames_to_deliver) != frames_to_deliver) {
            fprintf (stderr, "playback callback failed\n");
            break;
        }
    } // while

    if (!exit_flag) {
        //snd_pcm_nonblock(playback_handle, 0);
        if ((err = snd_pcm_drain (playback_handle)) < 0) {
            fprintf (stderr, "cannot drain pcm. [%d](%s)\n", err, snd_strerror (err));
        }
        else {
            fprintf (stderr, "pcm drawn.\n");
        }
        //snd_pcm_nonblock(playback_handle, 0);
    }

    snd_pcm_close (playback_handle);
    snd_output_close(logstd);
    if (file)
        fclose(file);
    exit (0);
}
