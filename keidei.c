/*
 * DRM driver for keidei displays
 *
 * Copyright 2018 Mihail Stefanov, 
 * Thanks to FREEWING.JP and Tong Zhang
 * 
 * https://github.com/FREEWING-JP/tinydrm/tree/feature/kedei_35_v50/kedei_35_lcd_v50
 * https://github.com/lzto/RaspberryPi_KeDei_35_lcd_v62
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <drm/tinydrm/mipi-dbi.h>
#include <drm/tinydrm/tinydrm-helpers.h>
#include <linux/gpio/consumer.h>
#include <video/mipi_display.h>

enum keidei_version
{
    KEIDEI_V10 = 1,
    KEIDEI_V20,
    KEIDEI_V50,
    KEIDEI_V60
};

#define KEIDEI20_RESET 0x00   /* 00000 */
#define KEIDEI20_NORESET 0x01 /* 00001 */
#define KEIDEI20_CMD_BE 0x11  /* 10001 */
#define KEIDEI20_CMD_AF 0x1B  /* 11011 */
#define KEIDEI20_DATA_BE 0x15 /* 10101 */
#define KEIDEI20_DATA_AF 0x1F /* 11111 */

static int spi_write60(struct mipi_dbi *mipi, const void *buf, size_t len)
{
    struct spi_device *spi = mipi->spi;
    int ret = 0;
    gpiod_set_value_cansleep(mipi->dc, 1);
    ret = spi_write(spi, buf, len);
    gpiod_set_value_cansleep(mipi->dc, 0);
    return ret;
}

static int keidei60_write8(struct mipi_dbi *mipi, u8 val, bool data)
{
    u8 buf[3];

    buf[0] = data ? KEIDEI20_DATA_BE : KEIDEI20_CMD_BE;
    buf[1] = 0x00;
    buf[2] = val;

    /* maybe this has to split up into 2 transfers, if it is too fast */
    DRM_DEBUG_DRIVER("%02x / %02x / %02x / %02x\n", val, buf[0], buf[1], buf[2]);

    return spi_write60(mipi, &buf[0], sizeof(buf));
}

static int keidei60_write16(struct mipi_dbi *mipi, u16 *pixel, int size)
{
    int ret;
    u16 val;
    u8 buf[3];

    for (; size > 0; --size)
    {
        val = *pixel++;
        buf[0] = KEIDEI20_DATA_BE;
        buf[1] = val >> 8;
        buf[2] = val & 0x00FF;

        /* maybe this has to split up into 2 transfers, if it is too fast */
        DRM_DEBUG_DRIVER("%02x / %02x / %02x / %02x\n", *pixel, buf[0], buf[1], buf[2]);

        ret = spi_write60(mipi, &buf[0], sizeof(buf));
        if (ret)
        {
            return ret;
        }
    }
    return 0;
}

static int keidei60_reset(struct mipi_dbi *mipi)
{
    u8 noreset[4] = {KEIDEI20_RESET, KEIDEI20_NORESET, KEIDEI20_RESET, KEIDEI20_RESET};
    u8 reset[4] = {KEIDEI20_RESET, KEIDEI20_RESET, KEIDEI20_RESET, KEIDEI20_RESET};
    int ret;

    ret = spi_write60(mipi, &noreset[0], sizeof(noreset));
    if (ret)
    {
        return ret;
    }
    msleep(50);

    spi_write60(mipi, &reset[0], sizeof(reset));
    msleep(100);

    spi_write60(mipi, &noreset[0], sizeof(noreset));
    msleep(50);

    return 0;
}

static int keidei60_prepare(struct mipi_dbi *mipi)
{
    struct gpio_desc *dc;
    struct device *dev = &mipi->spi->dev;
    int ret;

    DRM_DEBUG_KMS("Enable tsc gpio\n");
    dc = devm_gpiod_get(dev, "tsc", GPIOD_OUT_HIGH);
    if (IS_ERR(dc))
    {
        dev_err(dev, "Failed to get gpio 'tsc'\n");
        return PTR_ERR(dc);
    }
    DRM_DEBUG_KMS("tsc pin: %d\n", desc_to_gpio(dc));

    gpiod_set_value_cansleep(mipi->dc, 1);
    mipi->dc = dc;

    ret = keidei60_reset(mipi);
    if (ret)
    {
        dev_err(dev, "Failed to reset (%d)\n", ret);
        return ret;
    }

    mipi_dbi_command(mipi, 0x00);
    msleep(10);
    mipi_dbi_command(mipi, 0xff);
    mipi_dbi_command(mipi, 0xff);
    msleep(10);
    mipi_dbi_command(mipi, 0xff);
    mipi_dbi_command(mipi, 0xff);
    mipi_dbi_command(mipi, 0xff);
    mipi_dbi_command(mipi, 0xff);
    msleep(15);
    mipi_dbi_command(mipi, 0x11);
    msleep(150);

    mipi_dbi_command(mipi, 0xB0, 0x00);
    mipi_dbi_command(mipi, 0xB3, 0x02, 0x00, 0x00, 0x00);
    mipi_dbi_command(mipi, 0xB9, 0x01, 0x00, 0x0F, 0x0F);
    mipi_dbi_command(mipi, 0xC0, 0x13, 0x3B, 0x00, 0x02, 0x00, 0x01, 0x00, 0x43);
    mipi_dbi_command(mipi, 0xC1, 0x08, 0x0F, 0x08, 0x08);
    mipi_dbi_command(mipi, 0xC4, 0x11, 0x07, 0x03, 0x04);
    mipi_dbi_command(mipi, 0xC6, 0x00);
    mipi_dbi_command(mipi, 0xC8, 0x03, 0x03, 0x13, 0x5c, 0x03,
                     0x07, 0x14, 0x08, 0x00, 0x21,
                     0x08, 0x14, 0x07, 0x53, 0x0c,
                     0x13, 0x03, 0x03, 0x21, 0x00);
    mipi_dbi_command(mipi, 0x35, 0x00);
    mipi_dbi_command(mipi, 0x36, 0x60);
    mipi_dbi_command(mipi, 0x3a, 0x55);
    mipi_dbi_command(mipi, 0x44, 0x00, 0x01);
    mipi_dbi_command(mipi, 0xd0, 0x07, 0x07, 0x1d, 0x03);
    mipi_dbi_command(mipi, 0xd1, 0x03, 0x30, 0x10);
    mipi_dbi_command(mipi, 0xd2, 0x03, 0x14, 0x04);
    mipi_dbi_command(mipi, 0x29);

    msleep(30);

    mipi_dbi_command(mipi, 0x2a, 0x00, 0x00, 0x01, 0x3f);
    mipi_dbi_command(mipi, 0x2b, 0x00, 0x00, 0x01, 0xe0);
    mipi_dbi_command(mipi, 0xb4, 0x00);

    mipi_dbi_command(mipi, 0x2c);

    msleep(10);
    mipi_dbi_command(mipi, 0x36, 0b11101010);

    return 0;
}

static int keidei60_command(struct mipi_dbi *mipi, u8 cmd, u8 *par, size_t num)
{
    int i, ret;

    if (!num)
        DRM_DEBUG_DRIVER("cmd=%02x\n", cmd);
    else if (num <= 32)
        DRM_DEBUG_DRIVER("cmd=%02x, par=%*ph\n", cmd, (int)num, par);
    else
        DRM_DEBUG_DRIVER("cmd=%02x, len=%zu\n", cmd, num);

    ret = keidei60_write8(mipi, cmd, false);
    if (ret || !num)
        return ret;

    if (cmd == MIPI_DCS_WRITE_MEMORY_START)
    {
        u16 *pixel = (u16 *)par;
        keidei60_write16(mipi, pixel, (num / 2));
    }
    else
    {
        for (i = 0; i < num; i++)
            keidei60_write8(mipi, *par++, true);
    }

    return 0;
}

static int keidei50_reset(struct mipi_dbi *mipi)
{
    struct spi_device *spi = mipi->spi;
    u8 noreset[1] = {KEIDEI20_NORESET};
    u8 reset[1] = {KEIDEI20_RESET};
    int ret;

    ret = spi_write(spi, noreset, sizeof(noreset));
    if (ret)
        return ret;

    msleep(50);

    spi_write(spi, reset, sizeof(reset));
    msleep(100);

    spi_write(spi, noreset, sizeof(noreset));
    msleep(50);

    return 0;
}

static int keidei50_write8(struct spi_device *spi, u8 val, bool data)
{
    u8 buf[4];
    int ret;

    buf[0] = val >> 1;
    buf[1] = ((val & 1) << 5);

    buf[2] = buf[0];
    buf[3] = buf[1];

    buf[1] |= data ? KEIDEI20_DATA_BE : KEIDEI20_CMD_BE;

    /* maybe this has to split up into 2 transfers, if it is too fast */

    buf[3] |= data ? KEIDEI20_DATA_AF : KEIDEI20_CMD_AF;

    DRM_DEBUG_DRIVER("%02x /  %02x:%02x / %02x:%02x\n", val, buf[0], buf[1], buf[2], buf[3]);

    ret = spi_write(spi, &buf[0], 2);
    if (ret)
        return ret;

    return spi_write(spi, &buf[2], 2);
}

struct spi_message msg;
struct spi_transfer t[2];

static int keidei50_write16(struct spi_device *spi, u16 *pixel, int size)
{
    u8 buf[6];
    u8 pseudo;
    u16 val;
    int ret;

    for (; size > 0; --size)
    {
        val = *pixel++;

        pseudo = ((val >> 5) & 0x40) | ((val << 5) & 0x20);

        buf[0] = ((val >> 8) & 0xFF);
        buf[1] = (val & 0xFF);
        buf[2] = pseudo | KEIDEI20_DATA_BE;

        /* maybe this has to split up into 2 transfers, if it is too fast */

        buf[3] = buf[0];
        buf[4] = buf[1];
        buf[5] = pseudo | KEIDEI20_DATA_AF;

        DRM_DEBUG_DRIVER("%04x / %02x%02x:%02x / %02x%02x:%02x\n", val, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

        t[0].tx_buf = &buf[0];
        t[0].len = 3;
        t[1].tx_buf = &buf[3];
        t[1].len = 3;

        spi_message_init(&msg);
        spi_message_add_tail(&t[0], &msg);
        ret = spi_sync(spi, &msg);
        if (ret)
            break;

        spi_message_init(&msg);
        spi_message_add_tail(&t[1], &msg);
        ret = spi_sync(spi, &msg);
        if (ret)
            break;
    }
    return ret;
}

static int keidei50_command(struct mipi_dbi *mipi, u8 cmd, u8 *par, size_t num)
{
    struct spi_device *spi = mipi->spi;
    int i, ret;

    if (!num)
        DRM_DEBUG_DRIVER("cmd=%02x\n", cmd);
    else if (num <= 32)
        DRM_DEBUG_DRIVER("cmd=%02x, par=%*ph\n", cmd, (int)num, par);
    else
        DRM_DEBUG_DRIVER("cmd=%02x, len=%zu\n", cmd, num);

    ret = keidei50_write8(spi, cmd, false);
    if (ret || !num)
        return ret;

    if (cmd == MIPI_DCS_WRITE_MEMORY_START)
    {
        u16 *pixel = (u16 *)par;

        //		for (i = 0; i < num / 2; i++)
        //			keidei50_write16(spi, *pixel++, true);
        keidei50_write16(spi, pixel, (num / 2));
    }
    else
    {
        for (i = 0; i < num; i++)
            keidei50_write8(spi, *par++, true);
    }

    return 0;
}

static int keidei20_reset(struct mipi_dbi *mipi)
{
    struct spi_device *spi = mipi->spi;
    u8 noreset[3] = {0, 0, KEIDEI20_NORESET};
    u8 reset[3] = {0, 0, KEIDEI20_RESET};
    int ret;

    ret = spi_write(spi, noreset, sizeof(noreset));
    if (ret)
        return ret;

    msleep(50);

    spi_write(spi, reset, sizeof(reset));
    msleep(100);

    spi_write(spi, noreset, sizeof(noreset));
    msleep(50);

    return 0;
}

static int keidei20_write(struct spi_device *spi, u16 val, bool data)
{
    u8 buf[6];

    buf[0] = val >> 8;
    buf[1] = val;
    buf[2] = data ? KEIDEI20_DATA_BE : KEIDEI20_CMD_BE;

    /* maybe this has to split up into 2 transfers, if it is too fast */

    buf[3] = buf[0];
    buf[4] = buf[1];
    buf[5] = data ? KEIDEI20_DATA_AF : KEIDEI20_CMD_AF;

    DRM_DEBUG_DRIVER("%02x%02x:%02x / %02x%02x:%02x\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

    return spi_write(spi, buf, 6);
}

static int keidei20_command(struct mipi_dbi *mipi, u8 cmd, u8 *par, size_t num)
{
    struct spi_device *spi = mipi->spi;
    int i, ret;

    if (!num)
        DRM_DEBUG_DRIVER("cmd=%02x\n", cmd);
    else if (num <= 32)
        DRM_DEBUG_DRIVER("cmd=%02x, par=%*ph\n", cmd, (int)num, par);
    else
        DRM_DEBUG_DRIVER("cmd=%02x, len=%zu\n", cmd, num);

    ret = keidei20_write(spi, cmd, false);
    if (ret || !num)
        return ret;

    if (cmd == MIPI_DCS_WRITE_MEMORY_START)
    {
        u16 *pixel = (u16 *)par;

        for (i = 0; i < num / 2; i++)
            keidei20_write(spi, *pixel++, true);
    }
    else
    {
        for (i = 0; i < num; i++)
            keidei20_write(spi, *par++, true);
    }

    return 0;
}

static int keidei10_prepare(struct mipi_dbi *mipi)
{
    struct device *dev = &mipi->spi->dev;

    dev_err(dev, "Not supported (yet), just an example of multiple device support in one driver\n");

    return -ENODEV;
}

static int keidei20_prepare(struct mipi_dbi *mipi)
{
    struct device *dev = &mipi->spi->dev;
    int ret;

    DRM_DEBUG_KMS("\n");

    ret = keidei20_reset(mipi);
    if (ret)
    {
        dev_err(dev, "Failed to reset (%d)\n", ret);
        return ret;
    }

    mipi_dbi_command(mipi, 0x11);
    msleep(120);

    mipi_dbi_command(mipi, 0xee, 0x02, 0x01, 0x02, 0x01);
    mipi_dbi_command(mipi, 0xed, 0x00, 0x00, 0x9a, 0x9a, 0x9b,
                     0x9b, 0x00, 0x00, 0x00, 0x00,
                     0xae, 0xae, 0x01, 0xa2, 0x00);
    mipi_dbi_command(mipi, 0xb4, 0x00);
    mipi_dbi_command(mipi, 0xc0, 0x10, 0x3B, 0x00, 0x02, 0x11);
    mipi_dbi_command(mipi, 0xc1, 0x10);
    mipi_dbi_command(mipi, 0xc8, 0x00, 0x46, 0x12, 0x20,
                     0x0c, 0x00, 0x56, 0x12,
                     0x67, 0x02, 0x00, 0x0c);
    mipi_dbi_command(mipi, 0xd0, 0x44, 0x42, 0x06);
    mipi_dbi_command(mipi, 0xd1, 0x43, 0x16);
    mipi_dbi_command(mipi, 0xd2, 0x04, 0x22);
    mipi_dbi_command(mipi, 0xd3, 0x04, 0x12);
    mipi_dbi_command(mipi, 0xd4, 0x07, 0x12);
    mipi_dbi_command(mipi, 0xe9, 0x00);
    mipi_dbi_command(mipi, 0xc5, 0x08);
    mipi_dbi_command(mipi, 0x36, 0x6a);
    mipi_dbi_command(mipi, 0x3a, 0x55);

    mipi_dbi_command(mipi, 0x2a, 0x00, 0x00, 0x01, 0x3f);
    mipi_dbi_command(mipi, 0x2b, 0x00, 0x00, 0x01, 0xe0);
    msleep(120);

    mipi_dbi_command(mipi, 0x21);
    return 0;
}

static int keidei50_prepare(struct mipi_dbi *mipi)
{
    struct device *dev = &mipi->spi->dev;
    int ret;

    DRM_DEBUG_KMS("\n");

    ret = keidei50_reset(mipi);
    if (ret)
    {
        dev_err(dev, "Failed to reset (%d)\n", ret);
        return ret;
    }

    mipi_dbi_command(mipi, 0x00);
    mipi_dbi_command(mipi, 0x11);
    msleep(200);

    mipi_dbi_command(mipi, 0xEE, 0x02, 0x01, 0x02, 0x01);
    mipi_dbi_command(mipi, 0xED, 0x00, 0x00, 0x9A, 0x9A,
                     0x9B, 0x9B, 0x00, 0x00, 0x00, 0x00,
                     0xAE, 0xAE, 0x01, 0xA2, 0x00);
    mipi_dbi_command(mipi, 0xB4, 0x00);
    mipi_dbi_command(mipi, 0xC0, 0x10, 0x3B, 0x00, 0x02, 0x11);
    mipi_dbi_command(mipi, 0xC1, 0x10);
    mipi_dbi_command(mipi, 0xC8, 0x00, 0x46, 0x12, 0x20,
                     0x0C, 0x00, 0x56, 0x12, 0x67, 0x02, 0x00, 0x0C);
    mipi_dbi_command(mipi, 0xD0, 0x44, 0x42, 0x06);
    mipi_dbi_command(mipi, 0xD1, 0x43, 0x16);
    mipi_dbi_command(mipi, 0xD2, 0x04, 0x22);
    mipi_dbi_command(mipi, 0xD3, 0x04, 0x12);
    mipi_dbi_command(mipi, 0xD4, 0x07, 0x12);
    mipi_dbi_command(mipi, 0xE9, 0x00);
    mipi_dbi_command(mipi, 0xC5, 0x08);
    mipi_dbi_command(mipi, 0x36, 0x2A);
    mipi_dbi_command(mipi, 0x3A, 0x66);
    //	mipi_dbi_command(mipi, 0x2A, 0x00, 0x00, 0x01, 0x3F);
    //	mipi_dbi_command(mipi, 0x2B, 0x00, 0x00, 0x01, 0xE0);
    mipi_dbi_command(mipi, 0x35, 0x00);
    mipi_dbi_command(mipi, 0x29);
    msleep(200);
    mipi_dbi_command(mipi, 0x00);
    mipi_dbi_command(mipi, 0x11);
    msleep(200);
    mipi_dbi_command(mipi, 0xEE, 0x02, 0x01, 0x02, 0x01);
    mipi_dbi_command(mipi, 0xED, 0x00, 0x00, 0x9A, 0x9A, 0x9B,
                     0x9B, 0x00, 0x00, 0x00,
                     0x00, 0xAE, 0xAF, 0x01,
                     0xA2, 0x01, 0xBF, 0x2A);

    return 0;
}

static void keidei_enable(struct drm_simple_display_pipe *pipe,
                          struct drm_crtc_state *crtc_state)
{
    struct tinydrm_device *tdev = pipe_to_tinydrm(pipe);
    struct mipi_dbi *mipi = mipi_dbi_from_tinydrm(tdev);
    struct drm_framebuffer *fb = pipe->plane.fb;

    DRM_DEBUG_KMS("\n");

    mipi->enabled = true;
    fb->funcs->dirty(fb, NULL, 0, 0, NULL, 0);
}

static void keidei_disable(struct drm_simple_display_pipe *pipe)
{
    DRM_DEBUG_KMS("\n");
}

static const struct drm_simple_display_pipe_funcs keidei_funcs = {
    .enable = keidei_enable,
    .disable = keidei_disable,
    .update = tinydrm_display_pipe_update,
    .prepare_fb = tinydrm_display_pipe_prepare_fb,
};

static const struct drm_display_mode keidei_mode = {
    TINYDRM_MODE(480, 320, 0, 0),
};

static struct drm_driver keidei_driver = {
    .driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME |
                       DRIVER_ATOMIC,
    TINYDRM_GEM_DRIVER_OPS,
    .lastclose = tinydrm_lastclose,
    .debugfs_init = mipi_dbi_debugfs_init,
    .name = "keidei",
    .desc = "keidei",
    .date = "20180919",
    .major = 1,
    .minor = 0,
};

static const struct of_device_id keidei_of_match[] = {
    {.compatible = "keidei,keidei_v10", .data = (void *)KEIDEI_V10},
    {.compatible = "keidei,keidei_v20", .data = (void *)KEIDEI_V20},
    {.compatible = "keidei,keidei_v50", .data = (void *)KEIDEI_V50},
    {.compatible = "keidei,keidei_v60", .data = (void *)KEIDEI_V60},
    {},
};
MODULE_DEVICE_TABLE(of, keidei_of_match);

static int keidei_probe(struct spi_device *spi)
{
    const struct of_device_id *match;
    struct device *dev = &spi->dev;
    struct tinydrm_device *tdev;
    struct mipi_dbi *mipi;
    int ret = -ENODEV;

    if (!dev->coherent_dma_mask)
    {
        ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
        if (ret)
        {
            dev_warn(dev, "Failed to set dma mask %d\n", ret);
            return ret;
        }
    }

    match = of_match_device(keidei_of_match, dev);
    if (!match)
        return -ENODEV;

    mipi = devm_kzalloc(dev, sizeof(*mipi), GFP_KERNEL);
    if (!mipi)
        return -ENOMEM;

    mipi->spi = spi;

    switch ((enum keidei_version)match->data)
    {
    case KEIDEI_V10:
        mipi->command = NULL;
        break;
    case KEIDEI_V20:
        mipi->command = keidei20_command;
        break;
    case KEIDEI_V50:
        mipi->command = keidei50_command;
        break;
    case KEIDEI_V60:
        mipi->command = keidei60_command;
        break;
    }

    ret = mipi_dbi_init(dev, mipi, &keidei_funcs, &keidei_driver,
                        &keidei_mode, 0);
    if (ret)
        return ret;

    switch ((enum keidei_version)match->data)
    {
    case KEIDEI_V10:
        ret = keidei10_prepare(mipi);
        break;
    case KEIDEI_V20:
        ret = keidei20_prepare(mipi);
        break;
    case KEIDEI_V50:
        ret = keidei50_prepare(mipi);
        break;
    case KEIDEI_V60:
        ret = keidei60_prepare(mipi);
        break;
    }
    if (ret)
        return ret;

    tdev = &mipi->tinydrm;

    ret = devm_tinydrm_register(tdev);
    if (ret)
        return ret;

    spi_set_drvdata(spi, mipi);

    DRM_DEBUG_DRIVER("Initialized %s:%s @%uMHz on minor %d\n",
                     tdev->drm->driver->name, dev_name(dev),
                     spi->max_speed_hz / 1000000,
                     tdev->drm->primary->index);

    return 0;
}

static void keidei_shutdown(struct spi_device *spi)
{
    struct mipi_dbi *mipi = spi_get_drvdata(spi);
    tinydrm_shutdown(&mipi->tinydrm);
}

static struct spi_driver keidei_spi_driver = {
    .driver = {
        .name = "keidei",
        .owner = THIS_MODULE,
        .of_match_table = keidei_of_match,
    },
    .probe = keidei_probe,
    .shutdown = keidei_shutdown,
};
module_spi_driver(keidei_spi_driver);

/* Module autoloading */
MODULE_ALIAS("spi:keidei_v10");
MODULE_ALIAS("spi:keidei_v20");
MODULE_ALIAS("spi:keidei_v50");
MODULE_ALIAS("spi:keidei_v60");
MODULE_DESCRIPTION("keidei DRM driver");
MODULE_AUTHOR("Mihail Stefanov");
MODULE_LICENSE("GPL");