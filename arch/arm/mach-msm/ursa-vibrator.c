/* Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 * Copyright (c) 2012~2013, Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/pm8xxx/core.h>
#include <linux/mfd/pm8xxx/vibrator.h>
#include <linux/pwm.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <mach/gpio.h>
#include "../../../drivers/staging/android/timed_output.h"

#include <linux/timer.h>
#include <linux/jiffies.h>
#include "drivers_haptics_drv2604.h"
#include <linux/wakelock.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/earlysuspend.h>
#include <linux/firmware.h>

#define VIB_BRAKING_TIME	30 // 30msec for motor braking
#define DEFAULT_MAX_DURATION	15000 // 15 sec for maxmimum duration of vibration
#define VIB_ENABLE_PIN_INDEX	0 // vib_en pin is specified in the first "gpios" property
#define VIB_CONTROL_PIN_INDEX	1 // IN/TRIG pin (vib_control) is specified in the second "gpios" property

static const unsigned char autocal_sequence[] = {
    MODE_REG,                       AUTO_CALIBRATION,
        REAL_TIME_PLAYBACK_REG,         REAL_TIME_PLAYBACK_STRENGTH,
    GO_REG,                         GO,
};

static int drv260x_write_reg_val(struct i2c_client *client,const unsigned char* data,
    unsigned int size)
{
    int i = 0;
    int err = 0;

    if (size % 2 != 0)
        return -EINVAL;

    while (i < size)
    {
        err = i2c_smbus_write_byte_data(client, data[i], data[i+1]);
        if(err < 0){
            printk(KERN_ERR"%s, err=%d\n", __FUNCTION__, err);
            break;
        }
        i+=2;
    }

    return err;
}

static int drv260x_set_go_bit(struct i2c_client *client,char val)
{
    char go[] =
    {
        GO_REG, val
    };
    return drv260x_write_reg_val(client, go, sizeof(go));
}

static unsigned char drv260x_read_reg(struct i2c_client *client, unsigned char reg)
{
    return i2c_smbus_read_byte_data(client, reg);
}

static unsigned char drv260x_setbit_reg(struct i2c_client *client, unsigned char reg,
    unsigned char mask, unsigned char value)
{
    unsigned char temp = 0;
    unsigned char regval = drv260x_read_reg(client,reg);
    unsigned char buff[2];

    temp = regval & ~mask;
    temp |= value & mask;

    if(temp != regval){
        buff[0] = reg;
        buff[1] = temp;

        return drv260x_write_reg_val(client, buff, 2);
    }else
        return 2;
}

static void drv2604_poll_go_bit(struct i2c_client *client)
{
    while (drv260x_read_reg(client, GO_REG) == GO)
        schedule_timeout_interruptible(msecs_to_jiffies(GO_BIT_POLL_INTERVAL));
}

static int drv260x_set_rtp_val(struct i2c_client *client, char value)
{
    char rtp_val[] =
    {
        REAL_TIME_PLAYBACK_REG, value
    };
    return drv260x_write_reg_val(client, rtp_val, sizeof(rtp_val));
}

static int drv2604_set_waveform_sequence(struct i2c_client *client, unsigned char* seq,
    unsigned int size)
{
    struct drv2604_data *pdrv2604data = i2c_get_clientdata(client);
    unsigned char data[WAVEFORM_SEQUENCER_MAX + 1], i;
    int err = -1;

    if (size > WAVEFORM_SEQUENCER_MAX){
        printk(KERN_ERR"%s, sequence size overflow\n", __FUNCTION__);
        goto err;
    }

    for(i=0; i< size; i++){
        if(seq[i] > pdrv2604data->fw_header.fw_effCount){
            printk(KERN_ERR"%s, waveform id invalid\n", __FUNCTION__);
            goto err;
        }
    }

    memset(data, 0, sizeof(data));
    memcpy(&data[1], seq, size);
    data[0] = WAVEFORM_SEQUENCER_REG;

    err = i2c_master_send(client, data, sizeof(data));
err:

    return err;
}

static int drv260x_change_mode(struct i2c_client *client, char mode)
{
    struct drv2604_data *pdrv2604data = i2c_get_clientdata(client);
    unsigned char tmp[2] = {MODE_REG, mode};
    int err = 0;

    if(mode == MODE_PATTERN_RTP_ON)
        tmp[1] = MODE_REAL_TIME_PLAYBACK;
    else if(mode == MODE_PATTERN_RTP_OFF)
        tmp[1] = MODE_STANDBY;

    if(((mode == MODE_STANDBY) || (mode == MODE_PATTERN_RTP_OFF))
        &&((pdrv2604data->mode == MODE_PATTERN_RTP_OFF) || (pdrv2604data->mode == MODE_STANDBY))){
    }else if(mode != pdrv2604data->mode){
        if(mode != MODE_STANDBY || mode != MODE_PATTERN_RTP_OFF){
            gpio_direction_output(pdrv2604data->PlatData.GpioEnable, 1);
        }
        err = drv260x_write_reg_val(client, tmp, sizeof(tmp));
        schedule_timeout_interruptible(msecs_to_jiffies(STANDBY_WAKE_DELAY));
        if(mode == MODE_STANDBY || mode == MODE_PATTERN_RTP_OFF){
            gpio_direction_output(pdrv2604data->PlatData.GpioEnable, 0);
        }
    }

    pdrv2604data->mode = mode;

    return err;
}

#define YES 1
#define NO  0

static struct i2c_client *this_client;

static void vibrator_off(struct i2c_client *client)
{
    struct drv2604_data *pDrv2604data = i2c_get_clientdata(client);

    if (pDrv2604data->vibrator_is_playing) {
        pDrv2604data->vibrator_is_playing = NO;

        switch_set_state(&pDrv2604data->sw_dev, SW_STATE_IDLE);
        drv260x_change_mode(client, MODE_STANDBY);
    }

    wake_unlock(&pDrv2604data->wklock);
}


static void vibrator_work(struct work_struct *work)
{
    struct i2c_client *client = this_client;
    struct drv2604_data *pdrv2604data = i2c_get_clientdata(client);

    if(pdrv2604data->mode == MODE_PATTERN_RTP_ON){
        drv260x_change_mode(client, MODE_PATTERN_RTP_OFF);
        if(pdrv2604data->repeat_times == 0){
            drv260x_change_mode(client, MODE_STANDBY);
            pdrv2604data->vibrator_is_playing = NO;
            switch_set_state(&pdrv2604data->sw_dev, SW_STATE_IDLE);
        }else{
            hrtimer_start(&pdrv2604data->vib_timer, ns_to_ktime((u64)pdrv2604data->silence_time * NSEC_PER_MSEC), HRTIMER_MODE_REL);
        }
    }else if(pdrv2604data->mode == MODE_PATTERN_RTP_OFF){
        if(pdrv2604data->repeat_times > 0){
            pdrv2604data->repeat_times--;
            drv260x_change_mode(client, MODE_PATTERN_RTP_ON);
            hrtimer_start(&pdrv2604data->vib_timer, ns_to_ktime((u64)pdrv2604data->vibration_time * NSEC_PER_MSEC), HRTIMER_MODE_REL);
        }else{
            drv260x_change_mode(client, MODE_STANDBY);
            pdrv2604data->vibrator_is_playing = NO;
            switch_set_state(&pdrv2604data->sw_dev, SW_STATE_IDLE);
        }
    }else{
        vibrator_off(client);
    }
}

static void play_effect(struct work_struct *work){
    struct i2c_client *client = this_client;
    struct drv2604_data *pDrv2604data = i2c_get_clientdata(client);
    int err;

    switch_set_state(&pDrv2604data->sw_dev, SW_STATE_SEQUENCE_PLAYBACK);

    err = drv260x_change_mode(client, MODE_INTERNAL_TRIGGER);
    if(err < 0){
        goto err;
    }

    err = drv2604_set_waveform_sequence(client, pDrv2604data->sequence,
         sizeof(pDrv2604data->sequence));
    if(err < 0){
        goto err;
    }

    err = drv260x_set_go_bit(client, GO);
    if(err < 0){
        goto err;
    }

    while(drv260x_read_reg(client, GO_REG) == GO && !pDrv2604data->should_stop){
        schedule_timeout_interruptible(msecs_to_jiffies(GO_BIT_POLL_INTERVAL));
    }

    wake_unlock(&pDrv2604data->wklock);
    drv260x_change_mode(client, MODE_STANDBY);
err:
    switch_set_state(&pDrv2604data->sw_dev, SW_STATE_IDLE);
    return;
}

static ssize_t drv260x_read(struct file* filp, char* buff, size_t length, loff_t* offset)
{
    struct i2c_client *client = this_client;
    struct drv2604_data *pDrv2604data = i2c_get_clientdata(client);
    int ret = 0;

    if(pDrv2604data->pReadValue != NULL){

        ret = copy_to_user(buff,pDrv2604data->pReadValue, min((size_t)pDrv2604data->ReadLen, length));
        if (ret != 0){
            printk("%s, copy_to_user err=%d \n", __FUNCTION__, ret);
            ret = -1;
        }else{
            ret = min((size_t)pDrv2604data->ReadLen, length);
        }
        pDrv2604data->ReadLen = 0;
        kfree(pDrv2604data->pReadValue);
        pDrv2604data->pReadValue = NULL;

    }else{
        ret = copy_to_user(buff, &pDrv2604data->read_val, sizeof(pDrv2604data->read_val));
        if(ret != 0){
            printk("%s, copy_to_user err=%d \n", __FUNCTION__, ret);
            ret = -1;
        }
    }

    return ret;
}

static bool isforDebug(int cmd){
    return ((cmd == HAPTIC_CMDID_REG_WRITE)
        ||(cmd == HAPTIC_CMDID_REG_READ)
        ||(cmd == HAPTIC_CMDID_REG_SETBIT));
}

static ssize_t drv260x_write(struct file* filp, const char* buff, size_t len, loff_t* off)
{
    struct i2c_client *client = this_client;
    struct drv2604_data *pDrv2604data = i2c_get_clientdata(client);
    char data[8];

    mutex_lock(&pDrv2604data->lock);

    memset(data, 0, sizeof(data));
    if(len < 1 || copy_from_user(data, &buff[0], min(len, sizeof(data)))){
        printk("%s, Haptics write error\n", __FUNCTION__);
        mutex_unlock(&pDrv2604data->lock);
        return -1;
    }

    if(isforDebug(data[0])){
    }else{
        hrtimer_cancel(&pDrv2604data->vib_timer);

        pDrv2604data->should_stop = YES;
        cancel_work_sync(&pDrv2604data->work_play_eff);
        cancel_work_sync(&pDrv2604data->work);

        if (pDrv2604data->vibrator_is_playing)
        {
            pDrv2604data->vibrator_is_playing = NO;
            drv260x_change_mode(client, MODE_STANDBY);

        }
    }

    switch(data[0])
    {
        case HAPTIC_CMDID_PLAY_SINGLE_EFFECT:
        case HAPTIC_CMDID_PLAY_EFFECT_SEQUENCE:
        {
            memset(&pDrv2604data->sequence, 0, sizeof(pDrv2604data->sequence));
            if (len > 1)
            {
                memcpy(pDrv2604data->sequence, &buff[1], min(sizeof(pDrv2604data->sequence), len -1));
                pDrv2604data->should_stop = NO;
                wake_lock(&pDrv2604data->wklock);
                schedule_work(&pDrv2604data->work_play_eff);
            }
            else {
                printk("%s, Incorrect haptics format\n", __FUNCTION__);
                mutex_unlock(&pDrv2604data->lock);
                return -1;
            }
            break;
        }
        case HAPTIC_CMDID_PLAY_TIMED_EFFECT:
        {
            unsigned int value = 0;
            char mode;

            if(len != 3){
                printk("%s, Incorrect haptics format\n", __FUNCTION__);
                mutex_unlock(&pDrv2604data->lock);
                return -1;
            }

            value = data[2];
            value <<= 8;
            value |= data[1];

            if (value)
            {
                wake_lock(&pDrv2604data->wklock);

                mode = drv260x_read_reg(client, MODE_REG) & DRV260X_MODE_MASK;
                if (mode != MODE_REAL_TIME_PLAYBACK)
                {
                    if(mode == MODE_STANDBY){
                        drv260x_change_mode(client, MODE_INTERNAL_TRIGGER);
                    }

                    drv260x_set_rtp_val(client, REAL_TIME_PLAYBACK_STRENGTH);
                    drv260x_change_mode(client, MODE_REAL_TIME_PLAYBACK);
                    switch_set_state(&pDrv2604data->sw_dev, SW_STATE_RTP_PLAYBACK);
                    pDrv2604data->vibrator_is_playing = YES;
                }

                if (value > 0)
                {
                    if (value > MAX_TIMEOUT)
                        value = MAX_TIMEOUT;
                    hrtimer_start(&pDrv2604data->vib_timer, ns_to_ktime((u64)value * NSEC_PER_MSEC), HRTIMER_MODE_REL);
                }
            }
            break;
        }
        case HAPTIC_CMDID_PATTERN_RTP:
        {
            char mode;
            unsigned char strength = 0;

            if(len != 7){
                printk("%s, Incorrect haptics format\n", __FUNCTION__);
                mutex_unlock(&pDrv2604data->lock);
                return -1;
            }

            pDrv2604data->vibration_time = (int)((((int)data[2])<<8) | (int)data[1]);
            pDrv2604data->silence_time = (int)((((int)data[4])<<8) | (int)data[3]);
            pDrv2604data->repeat_times = data[5];
            strength = data[6];

            if(pDrv2604data->vibration_time > 0){
                mode = drv260x_read_reg(client, MODE_REG) & DRV260X_MODE_MASK;
                if (mode != MODE_REAL_TIME_PLAYBACK){
                    if(mode == MODE_STANDBY){
                        drv260x_change_mode(client, MODE_INTERNAL_TRIGGER);
                    }

                    drv260x_set_rtp_val(client, strength);
                    drv260x_change_mode(client, MODE_PATTERN_RTP_ON);
                    if(pDrv2604data->repeat_times > 0)
                        pDrv2604data->repeat_times--;
                    switch_set_state(&pDrv2604data->sw_dev, SW_STATE_RTP_PLAYBACK);
                    pDrv2604data->vibrator_is_playing = YES;
                }

                if (pDrv2604data->vibration_time > MAX_TIMEOUT)
                    pDrv2604data->vibration_time = MAX_TIMEOUT;

                hrtimer_start(&pDrv2604data->vib_timer, ns_to_ktime((u64)pDrv2604data->vibration_time * NSEC_PER_MSEC), HRTIMER_MODE_REL);
            }
            break;
        }
        case HAPTIC_CMDID_STOP:
        {
            if (pDrv2604data->vibrator_is_playing)
            {
                pDrv2604data->vibrator_is_playing = NO;
                switch_set_state(&pDrv2604data->sw_dev, SW_STATE_IDLE);
                drv260x_change_mode(client, MODE_STANDBY);
            }
            pDrv2604data->should_stop = YES;
            break;
        }
        case HAPTIC_CMDID_GET_DEV_ID:
        {
            // Dev ID includes 2 parts, upper word for device id, lower word for chip revision
            int revision = (drv260x_read_reg(client, SILICON_REVISION_REG) & SILICON_REVISION_MASK);
            pDrv2604data->read_val = (pDrv2604data->device_id >> 1) | revision;
            break;
        }
        case HAPTIC_CMDID_RUN_DIAG:
        {
            char diag_seq[] =
            {
                MODE_REG, MODE_DIAGNOSTICS,
                GO_REG,   GO
            };

            drv260x_write_reg_val(client, diag_seq, sizeof(diag_seq));
            drv2604_poll_go_bit(client);
            pDrv2604data->read_val = (drv260x_read_reg(client, STATUS_REG) & DIAG_RESULT_MASK) >> 3;
            break;
        }
    default:
        printk("%s, unknown HAPTIC cmd\n", __FUNCTION__);
      break;
    }

    mutex_unlock(&pDrv2604data->lock);

    return len;
}

static struct file_operations fops =
{
    .read = drv260x_read,
    .write = drv260x_write
};

static enum hrtimer_restart ursa_vib_timer_func(struct hrtimer *timer)
{
	struct drv2604_data *vib = container_of(timer, struct drv2604_data,
							 vib_timer);

	schedule_work(&vib->work);

	return HRTIMER_NORESTART;
}


static int ursa_vib_get_time(struct timed_output_dev *dev)
{
	struct drv2604_data *vib = container_of(dev, struct drv2604_data,
							 timed_dev);

	if (hrtimer_active(&vib->vib_timer)) {
		ktime_t r = hrtimer_get_remaining(&vib->vib_timer);
		return (int)ktime_to_us(r);
	} else
		return 0;
}


static void ursa_vib_enable(struct timed_output_dev *dev, int value)
{
    struct i2c_client *client = this_client;
    struct drv2604_data *pDrv2604data = i2c_get_clientdata(client);
    char mode;

    mutex_lock(&pDrv2604data->lock);
    cancel_work_sync(&pDrv2604data->work);

    if (value) {
        wake_lock(&pDrv2604data->wklock);

        mode = drv260x_read_reg(client, MODE_REG) & DRV260X_MODE_MASK;
        /* Only change the mode if not already in RTP mode; RTP input already set at init */
        if (mode != MODE_REAL_TIME_PLAYBACK){
            drv260x_set_rtp_val(client, REAL_TIME_PLAYBACK_STRENGTH);
            drv260x_change_mode(client, MODE_REAL_TIME_PLAYBACK);
            pDrv2604data->vibrator_is_playing = YES;
            switch_set_state(&pDrv2604data->sw_dev, SW_STATE_RTP_PLAYBACK);
        }

        if (value > 0) {
            if (value > MAX_TIMEOUT)
                value = MAX_TIMEOUT;
            hrtimer_start(&pDrv2604data->vib_timer, ns_to_ktime((u64)value * NSEC_PER_MSEC), HRTIMER_MODE_REL);
        }
    }
    else
        vibrator_off(client);

    mutex_unlock(&pDrv2604data->lock);
}


static int Haptics_init(struct drv2604_data *pDrv2604Data)
{
    int reval = -ENOMEM;


    pDrv2604Data->version = MKDEV(0,0);
    reval = alloc_chrdev_region(&pDrv2604Data->version, 0, 1, HAPTICS_DEVICE_NAME);
    if (reval < 0)
    {
        printk(KERN_ALERT"drv260x: error getting major number %d\n", reval);
        goto fail0;
    }

    pDrv2604Data->class = class_create(THIS_MODULE, HAPTICS_DEVICE_NAME);
    if (!pDrv2604Data->class)
    {
        printk(KERN_ALERT"drv260x: error creating class\n");
        goto fail1;
    }

    pDrv2604Data->device = device_create(pDrv2604Data->class, NULL,
        pDrv2604Data->version, NULL, HAPTICS_DEVICE_NAME);
    if (!pDrv2604Data->device)
    {
        printk(KERN_ALERT"drv260x: error creating device 2604\n");
        goto fail2;
    }

    cdev_init(&pDrv2604Data->cdev, &fops);
    pDrv2604Data->cdev.owner = THIS_MODULE;
    pDrv2604Data->cdev.ops = &fops;
    reval = cdev_add(&pDrv2604Data->cdev, pDrv2604Data->version, 1);

    if (reval)
    {
        printk(KERN_ALERT"drv260x: fail to add cdev\n");
        goto fail3;
    }

    pDrv2604Data->sw_dev.name = "haptics";
    reval = switch_dev_register(&pDrv2604Data->sw_dev);
    if (reval < 0) {
        printk(KERN_ALERT"drv260x: fail to register switch\n");
        goto fail4;
    }

    pDrv2604Data->timed_dev.name = "vibrator";
    pDrv2604Data->timed_dev.get_time = ursa_vib_get_time;
    pDrv2604Data->timed_dev.enable = ursa_vib_enable;

    if (timed_output_dev_register(&pDrv2604Data->timed_dev) < 0)
    {
        printk(KERN_ALERT"drv260x: fail to create timed output dev\n");
        goto fail3;
    }

    hrtimer_init(&pDrv2604Data->vib_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    pDrv2604Data->vib_timer.function = ursa_vib_timer_func;
    INIT_WORK(&pDrv2604Data->work, vibrator_work);
    INIT_WORK(&pDrv2604Data->work_play_eff, play_effect);

    wake_lock_init(&pDrv2604Data->wklock, WAKE_LOCK_SUSPEND, "vibrator");
    mutex_init(&pDrv2604Data->lock);

    printk(KERN_ALERT"drv260x: initialized\n");
    return 0;

fail4:
    switch_dev_unregister(&pDrv2604Data->sw_dev);
fail3:
    device_destroy(pDrv2604Data->class, pDrv2604Data->version);
fail2:
    class_destroy(pDrv2604Data->class);
fail1:
    unregister_chrdev_region(pDrv2604Data->version, 1);
fail0:
    return reval;
}

static void dev_init_platform_data(struct i2c_client* client, struct drv2604_data *pdrv2604data)
{
    struct drv2604_platform_data *pdrv2604Platdata = &pdrv2604data->PlatData;
    struct actuator_data actuator = pdrv2604Platdata->actuator;
    unsigned char loop = 0;
    unsigned char tmp[8] = {0};

    //OTP memory saves data from 0x16 to 0x1a
    if(pdrv2604data->OTP == 0) {
        if(actuator.rated_vol != 0){
            tmp[0] = RATED_VOLTAGE_REG;
            tmp[1] = actuator.rated_vol;
            drv260x_write_reg_val(client, tmp, 2);
        }else{
            printk("%s, vib ERROR Rated ZERO\n", __FUNCTION__);
        }

        if(actuator.over_drive_vol != 0){
            tmp[0] = OVERDRIVE_CLAMP_VOLTAGE_REG;
            tmp[1] = actuator.over_drive_vol;
            drv260x_write_reg_val(client, tmp, 2);
        }else{
            printk("%s, vib ERROR OverDriveVol ZERO\n", __FUNCTION__);
        }


        //Setting drv2604 to LRA mode. Setting drv2604 to ERM mode will break the
        //vibrator, so don't do that.
        drv260x_setbit_reg(client,
                        FEEDBACK_CONTROL_REG,
                        FEEDBACK_CONTROL_DEVICE_TYPE_MASK,
                        FEEDBACK_CONTROL_MODE_LRA);
    }else{
        printk("%s, vib OTP programmed\n", __FUNCTION__);
    }

    if(actuator.drive_time != DEFAULT_DRIVE_TIME){
        drv260x_setbit_reg(client,
                Control1_REG,
                Control1_REG_DRIVE_TIME_MASK,
                actuator.drive_time);
    }

    if(actuator.loop == OPEN_LOOP){
        if(actuator.device_type == LRA)
            loop = 0x01;
        else if(actuator.device_type == ERM)
            loop = ERM_OpenLoop_Enabled;
    }

    drv260x_setbit_reg(client,
                    Control3_REG,
                    Control3_REG_LOOP_MASK,
                    loop);
}

#ifdef RUN_AUTOCALIBRATE
static int dev_auto_calibrate(struct i2c_client* client, struct drv2604_data *pdrv2604data)
{
    int err = 0, status=0;
    err = drv260x_write_reg_val(client, autocal_sequence, sizeof(autocal_sequence));
    pdrv2604data->mode = AUTO_CALIBRATION;

    // Wait until the procedure is done
    drv2604_poll_go_bit(client);
    // Read status
    status = drv260x_read_reg(client, STATUS_REG);
    if(pdrv2604data->device_id != (status & DEV_ID_MASK)){
        printk("%s, ERROR after calibration status =0x%x\n", __FUNCTION__, status);
        return -ENODEV;
    }

    // Check result
    if ((status & DIAG_RESULT_MASK) == AUTO_CAL_FAILED)
    {
        printk(KERN_ALERT"drv260x auto-cal failed.\n");
        drv260x_write_reg_val(client, autocal_sequence, sizeof(autocal_sequence));
        drv2604_poll_go_bit(client);
        status = drv260x_read_reg(client, STATUS_REG);
        if ((status & DIAG_RESULT_MASK) == AUTO_CAL_FAILED)
        {
            printk(KERN_ALERT"drv260x auto-cal retry failed.\n");
            // return -ENODEV;
        }
    }

    // Read calibration results
    drv260x_read_reg(client, AUTO_CALI_RESULT_REG);
    drv260x_read_reg(client, AUTO_CALI_BACK_EMF_RESULT_REG);
    drv260x_read_reg(client, FEEDBACK_CONTROL_REG);

    return err;
}
#endif

static int fw_chksum(const struct firmware *fw){
    int sum = 0;
    int i=0;
    int size = fw->size;
    const unsigned char *pBuf = fw->data;

    for (i=0; i< size; i++){
        if((i>11) && (i<16)){

        }else{
            sum += pBuf[i];
        }
    }

    return sum;
}

/* drv2604_firmware_load:   This function is called by the
 *      request_firmware_nowait function as soon
 *      as the firmware has been loaded from the file.
 *      The firmware structure contains the data and$
 *      the size of the firmware loaded.
 * @fw: pointer to firmware file to be dowloaded
 * @context: pointer variable to drv2604_data
 *
 */
static void drv2604_firmware_load(const struct firmware *fw, void *context)
{
    struct drv2604_data *pDrv2604data = context;
    int size = 0;
    int fwsize = 0;
    int i=0;
    const unsigned char *pBuf = NULL;
    unsigned char writeBuf[4] = {0};
    if(fw != NULL){
        pBuf = fw->data;
        size = fw->size;

        memcpy(&(pDrv2604data->fw_header), pBuf, sizeof(struct drv2604_fw_header));
        if((pDrv2604data->fw_header.fw_magic != DRV2604_MAGIC)
            ||(pDrv2604data->fw_header.fw_size != size)
            ||(pDrv2604data->fw_header.fw_chksum != fw_chksum(fw))){
            printk("%s, ERROR!! firmware not right:Magic=0x%x,Size=%d,chksum=0x%x\n",
                __FUNCTION__, pDrv2604data->fw_header.fw_magic,
                pDrv2604data->fw_header.fw_size, pDrv2604data->fw_header.fw_chksum);
        }else{
            printk("%s, firmware good\n", __FUNCTION__);

            drv260x_change_mode(pDrv2604data->client, MODE_INTERNAL_TRIGGER);

            pBuf += sizeof(struct drv2604_fw_header);

            writeBuf[0] = DRV2604_REG_RAM_ADDR_UPPER_BYTE;
            writeBuf[1] = 0;
            writeBuf[2] = DRV2604_REG_RAM_ADDR_LOWER_BYTE;
            writeBuf[3] = 0;

            drv260x_write_reg_val(pDrv2604data->client, writeBuf, 4);

            fwsize = size - sizeof(struct drv2604_fw_header);
            for(i = 0; i < fwsize; i++){
                writeBuf[0] = DRV2604_REG_RAM_DATA;
                writeBuf[1] = pBuf[i];
                drv260x_write_reg_val(pDrv2604data->client, writeBuf, 2);
            }
        }

        drv260x_change_mode(pDrv2604data->client, MODE_STANDBY);

        release_firmware(fw);
    }else{
        printk("%s, ERROR!! firmware not found\n", __FUNCTION__);
    }
}

#ifdef CONFIG_PM
static int ursa_vib_suspend(struct device *dev)
{
	struct drv2604_data *vib = dev_get_drvdata(dev);
    struct i2c_client *client = vib->client;
	hrtimer_cancel(&vib->vib_timer);
	cancel_work_sync(&vib->work);
	/* turn-off vibrator */
    vibrator_off(client);
	return 0;
}

static const struct dev_pm_ops ursa_vib_pm_ops = {
	.suspend = ursa_vib_suspend,
};
#endif

static int ursa_read_devnode(struct drv2604_data *vib, struct device_node *np)
{
	int rc;

	if(!vib || !np)
		return -EINVAL;

	/* read inital vibration duration */
	rc = of_property_read_u32(np, "ursa,max-duration-ms", &(vib->max_duration));
	if (rc) { // fall back to default value if read failed
		vib->max_duration = DEFAULT_MAX_DURATION;
	}

	/* read inital vibration duration */
	rc = of_property_read_u32(np, "ursa,vib-init-duration", &(vib->init_duration));
	if (rc) { // if not specified then no vibration during the boot
		vib->init_duration = 0;
	}

	/* retrieve vib_enable GPIO pin number for linux side usage */
	vib->PlatData.GpioEnable = of_get_gpio(np, VIB_ENABLE_PIN_INDEX);
	if (!gpio_is_valid(vib->PlatData.GpioEnable)) {
		rc = -EINVAL;
		dev_err(&vib->dev, "%s: Invalid vib_en gpio number %d \n", __func__, vib->PlatData.GpioEnable);
		goto err_read_vib;
	}

	/* retrieve vib_control (IN/TRIG) GPIO pin number for linux side usage */
	vib->PlatData.GpioTrigger = of_get_gpio(np, VIB_CONTROL_PIN_INDEX);
	if (!gpio_is_valid(vib->PlatData.GpioTrigger)) {
		rc = -EINVAL;
		dev_err(&vib->dev, "%s: Invalid vib_control gpio number %d \n", __func__, vib->PlatData.GpioTrigger);
		goto err_read_vib;
	}

	return 0;

err_read_vib:
	return rc;
}

static int __devinit ursa_vib_probe(struct i2c_client *client,
		const struct i2c_device_id *dev_id)
{
	struct drv2604_data *vib;
	int rc;
    //struct drv2604_platform_data *platdata = client->dev.platform_data;
    int err = 0;
    int status = 0;
    #ifndef RUN_AUTOCALIBRATE
    unsigned char buf[2];
    #endif

	vib = kzalloc(sizeof(*vib), GFP_KERNEL);
	if (!vib)
		return -ENOMEM;

	vib->dev = client->dev;
    this_client = client;
    vib->client = client;
	i2c_set_clientdata(client, vib);
	
    rc = ursa_read_devnode(vib, vib->dev.of_node);
	if (rc) {
		dev_err(&client->dev, "%s: Error happened when retrieving configuration data from device tree node!\n", __func__);
		goto err_init_vib;
	}

	/* request access to vib_en gpio */
	rc = gpio_request(vib->PlatData.GpioEnable, "vib_gpio_enable");
	if (rc) {
		dev_err(&client->dev, "%s: request vib enable GPIO %d failed, rc=%d\n",
								__func__, vib->PlatData.GpioEnable, rc);
		goto err_init_vib;
	}

	/* request access to vib_control gpio */
	rc = gpio_request(vib->PlatData.GpioTrigger, "vib_gpio_control");
	if (rc) {
		dev_err(&client->dev, "%s: request vib control GPIO %d failed, rc=%d\n",
								__func__, vib->PlatData.GpioTrigger, rc);
		goto err_control_request;
	}

    /* Enable power to the chip */
    gpio_direction_output(vib->PlatData.GpioEnable, 1);

    /* Wait 30 us */
    udelay(30);

    //Sets data for auto calibration
    vib->PlatData.actuator.rated_vol = 0x50;
    vib->PlatData.actuator.over_drive_vol = 0x92;
    vib->PlatData.actuator.loop = CLOSE_LOOP;
    vib->PlatData.actuator.drive_time = DEFAULT_DRIVE_TIME;
    vib->PlatData.actuator.device_type = LRA;

    status = drv260x_read_reg(vib->client, STATUS_REG);
    /* Read device ID */
    vib->device_id = (status & DEV_ID_MASK);
    if(vib->device_id == DRV2604){
        printk(KERN_ALERT"drv260x driver found: drv2604.\n");
        status = request_firmware_nowait(THIS_MODULE,
                FW_ACTION_HOTPLUG,
                "drv2604.bin",
                &(vib->client->dev),
                GFP_KERNEL,
                vib,
                drv2604_firmware_load);
    }else{
        printk("%s, status(0x%x),device_id(%d) fail\n",
            __FUNCTION__, status, vib->device_id);
        goto err_LRA_mode;
    }

    vib->mode = MODE_STANDBY;

    drv260x_change_mode(vib->client, MODE_INTERNAL_TRIGGER);
    schedule_timeout_interruptible(msecs_to_jiffies(STANDBY_WAKE_DELAY));

    vib->OTP = drv260x_read_reg(vib->client,
        AUTOCAL_MEM_INTERFACE_REG) & AUTOCAL_MEM_INTERFACE_REG_OTP_MASK;

    dev_init_platform_data(vib->client, vib);

    if(!(drv260x_read_reg(vib->client, 0x1a) & 0x80)){
        printk("ERROR: drv2604 not set to LRA mode, quitting now\n");
        goto err_LRA_mode;
    }

    #ifdef RUN_AUTOCALIBRATE
    if(vib->OTP == 0){
        err = dev_auto_calibrate(vib->client, vib);
        if(err < 0){
            printk("%s, ERROR, calibration fail\n", __FUNCTION__);
        }
    }
    #else
    //do not want auto calibrate to run on every boot, therefore
    //prepopulate auto calibration registers with values
    buf[0] = AUTO_CALI_RESULT_REG;
    buf[1] = 0x07;
    drv260x_write_reg_val(vib->client, buf, sizeof(buf));

    buf[0] = AUTO_CALI_BACK_EMF_RESULT_REG;
    buf[1] = 0x7c;
    drv260x_write_reg_val(vib->client, buf, sizeof(buf));

    buf[0] = FEEDBACK_CONTROL_REG;
    buf[1] = 0xb5;
    drv260x_write_reg_val(vib->client, buf, sizeof(buf));
    #endif
    drv260x_change_mode(vib->client, MODE_STANDBY);

    err = Haptics_init(vib);

    if(err < 0){
        printk("%s, Haptics_init fail!\n", __FUNCTION__);
        goto err_LRA_mode;
    }


	/* do the first vibration if specified */
	if (vib->init_duration) {
		ursa_vib_enable(&vib->timed_dev, vib->init_duration);
	}

    printk("drv260x probe succeeded\n");

	return 0;

err_LRA_mode:
	gpio_free(vib->PlatData.GpioTrigger);
err_control_request:
	gpio_free(vib->PlatData.GpioEnable);
err_init_vib:
	kfree(vib);
	return rc;
}

static int __devexit ursa_vib_remove(struct i2c_client *client)
{
	struct drv2604_data *vib = i2c_get_clientdata(client);

    device_destroy(vib->class, vib->version);
    class_destroy(vib->class);
    unregister_chrdev_region(vib->version, 1);

	cancel_work_sync(&vib->work);
	timed_output_dev_unregister(&vib->timed_dev);
	hrtimer_cancel(&vib->vib_timer);
	gpio_free(vib->PlatData.GpioEnable);
	gpio_free(vib->PlatData.GpioTrigger);
	kfree(vib);

	return 0;
}

#define URSA_VIBRATOR_COMPAT_NAME "lab126,ursa-vibrator"
#define DRIVER_NAME "ti_drv2604"

static struct of_device_id ursa_vib_match_table[] = {
	{ .compatible = URSA_VIBRATOR_COMPAT_NAME, },
	{}
};

static const struct i2c_device_id ti_drv2604_id_table[] = {
	{DRIVER_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ti_drv2604_id_table);

static struct i2c_driver ursa_vib_driver = {
	.probe		= ursa_vib_probe,
	.remove		= __devexit_p(ursa_vib_remove),
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = ursa_vib_match_table,
#ifdef CONFIG_PM
		.pm	= &ursa_vib_pm_ops,
#endif
	},
	.id_table = ti_drv2604_id_table,
};

static int __init ursa_vib_init(void)
{
	int rc;

	rc = i2c_add_driver(&ursa_vib_driver);
	if (rc) {
		pr_err("%s: i2c_add_driver call failed! rc = %d\n", __func__, rc);
	}

	return rc;
}

static void __exit ursa_vib_exit(void)
{
	i2c_del_driver(&ursa_vib_driver);
}

late_initcall(ursa_vib_init);
module_exit(ursa_vib_exit);

MODULE_ALIAS("ursa vibrator:" DRIVER_NAME);
MODULE_DESCRIPTION("ursa vibrator driver");
MODULE_LICENSE("GPL v2");
