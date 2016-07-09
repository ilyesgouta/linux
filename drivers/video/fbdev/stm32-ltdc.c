/*
 * Copyright (C) 2016 Ilyes Gouta, ilyes.gouta@gmail.com
 *
 * Inspired from linux/drivers/video/atmel-lcdfb.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 *  A driver for the LTDC LCD controller on STM32
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/hardirq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <asm/sizes.h>

#define LTDC_L1CFBAR	0xac /* fb start address */
#define LTDC_L1CFBLR	0xb0 /* fb length */
#define LTDC_L1PFCR	0x94 /* fb pixel format */
#define LTDC_L1DCCR	0x9c /* fill color */
#define LTDC_L1CR	0x84 /* layer control register */
#define LTDC_BCCR	0x2c /* background color */
#define LTDC_IER	0x34 /* interrupts enable */
#define LTDC_ICR	0x3C /* interrupts clear */
#define LTDC_GCR	0x18 /* global control */
#define LTDC_SSCR	0x8 /* synchronization size */
#define LTDC_BPCR	0xc /* back porch */
#define LTDC_AWCR	0x10 /* active width */
#define LTDC_TWCR	0x14 /* total width */

#define CNTL_LCDEN	1
#define LTDC_ARGB	0

struct ltdc_panel {
	struct fb_videomode	mode;
	signed short		width;	/* width in mm */
	signed short		height;	/* height in mm */
	u32			tim2;
	u32			tim3;
	u32			cntl;
	u32			caps;
	unsigned int		bpp:8,
				fixedtimings:1,
				grayscale:1;
	unsigned int		connector;
};

struct ltdc_fb {
	struct fb_info		fb;
	struct platform_device	*pdev;
	struct clk		*clk;
	void __iomem		*regs;
	struct ltdc_panel	*panel;
	u32			ltdc_cntl;
	u32			cmap[16];
	u32			irq;
	u32			error_irq;
};

#define to_ltdc(info)	container_of(info, struct ltdc_fb, fb)

static const char *ltdc_name = "LTDC FB";

static inline void ltdcfb_set_start(struct ltdc_fb *fb)
{
	unsigned long ustart = fb->fb.fix.smem_start;
	unsigned long lstart, len;

	ustart += fb->fb.var.yoffset * fb->fb.fix.line_length;
	lstart = ustart + fb->fb.var.yres * fb->fb.fix.line_length;

	len = (fb->fb.fix.line_length + 3) | (fb->fb.fix.line_length << 16);

	writel(lstart, fb->regs + LTDC_L1CFBAR);
	writel(len, fb->regs + LTDC_L1CFBLR);
	writel(LTDC_ARGB, fb->regs + LTDC_L1PFCR);
}

static void ltdcfb_disable(struct ltdc_fb *fb)
{
	u32 val;

	val = readl(fb->regs + LTDC_L1CR);
	val &= ~CNTL_LCDEN;
	writel(val, fb->regs + LTDC_L1CR);
}

static void ltdcfb_enable(struct ltdc_fb *fb)
{
	u32 val;

	val = readl(fb->regs + LTDC_L1CR);
	val |= CNTL_LCDEN;
	writel(val, fb->regs + LTDC_L1CR);
}

static int
ltdcfb_set_bitfields(struct ltdc_fb *fb, struct fb_var_screeninfo *var)
{
	int ret = 0;

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	switch (var->bits_per_pixel) {
	case 32:
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.length = 8;
		var->blue.offset = 0;
		var->green.offset = var->blue.offset + var->blue.length;
		var->red.offset = var->green.offset + var->green.length;
		var->transp.offset = var->red.offset + var->red.length;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ltdcfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct ltdc_fb *fb = to_ltdc(info);
	int ret = -EINVAL;

	if ((var->xres_virtual
	     * (var->bits_per_pixel / 8)
	     * var->yres_virtual) > fb->fb.fix.smem_len)
		ret = -EINVAL;

	/* for now only 32bpp is supported */

	return ltdcfb_set_bitfields(fb, var);
}

static int ltdcfb_set_par(struct fb_info *info)
{
	struct ltdc_fb *fb = to_ltdc(info);

	fb->fb.fix.line_length = fb->fb.var.xres_virtual *
				 fb->fb.var.bits_per_pixel / 8;

	if (fb->fb.var.bits_per_pixel <= 8)
		fb->fb.fix.visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fb->fb.fix.visual = FB_VISUAL_TRUECOLOR;

	ltdcfb_set_start(fb);

//	clk_set_rate(fb->clk, (1000000000 / regs.pixclock) * 1000);

	return 0;
}

static int
ltdcfb_setcolreg(unsigned int regno, unsigned int red, unsigned int green,
		 unsigned int blue, unsigned int transp, struct fb_info *info)
{
	return -EINVAL;
}

static int ltdcfb_blank(int blank_mode, struct fb_info *info)
{
//	struct ltdc_fb *fb = to_ltdc(info);

//	if (blank_mode != 0) {
//		ltdcfb_disable(fb);
//	} else {
//		ltdcfb_enable(fb);
//	}
	return 0;
}

static int ltdcfb_mmap(struct fb_info *info,
		       struct vm_area_struct *vma)
{
	struct ltdc_fb *fb = to_ltdc(info);
	unsigned long len, off = vma->vm_pgoff << PAGE_SHIFT;
	int ret = -EINVAL;

	len = info->fix.smem_len;

	if (off <= len && (vma->vm_end - vma->vm_start) <= (len - off))
		ret = dma_mmap_wc(&fb->pdev->dev, vma, fb->fb.screen_base,
				  fb->fb.fix.smem_start + off, len - off);

	return ret;
}

static struct fb_ops ltdcfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= ltdcfb_check_var,
	.fb_set_par	= ltdcfb_set_par,
	.fb_setcolreg	= ltdcfb_setcolreg,
	.fb_blank	= ltdcfb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_mmap	= ltdcfb_mmap,
};

static irqreturn_t ltdc_interrupt(int irq, void *dev_id)
{
	struct ltdc_fb *fb = (struct ltdc_fb *)dev_id;

	writel(0xf, fb->regs + LTDC_ICR);

	return IRQ_HANDLED;
}

static int ltdcfb_register(struct ltdc_fb *fb)
{
	dma_addr_t dma;
	struct device *dev = &fb->pdev->dev;
	struct resource *regs = NULL;
	u32 framesize = 480 * 272 * 4; /* one ARGB buffer */
	int ret;

	fb->clk = devm_clk_get(&fb->pdev->dev, "ltdc-clk");
	if (IS_ERR(fb->clk)) {
		ret = PTR_ERR(fb->clk);
		goto out;
	}

	ret = clk_prepare_enable(fb->clk);
	if (ret)
		goto free_clk;

	fb->fb.device = &fb->pdev->dev;

	regs = platform_get_resource(fb->pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(dev, "resources unusable\n");
		ret = -ENXIO;
		goto clk_unprep;
	}

	fb->fb.fix.mmio_start	= regs->start;
	fb->fb.fix.mmio_len	= resource_size(regs);

	fb->regs = ioremap(fb->fb.fix.mmio_start, fb->fb.fix.mmio_len);
	if (!fb->regs) {
		pr_err("%s: unable to remap registers\n", __func__);
		ret = -ENOMEM;
		goto clk_unprep;
	}

	fb->irq = platform_get_irq_byname(fb->pdev, "ltdc-irq");
	fb->error_irq = platform_get_irq_byname(fb->pdev, "ltdc-error-irq");
	if (fb->irq < 0
	    || fb->error_irq < 0) {
		pr_err("%s: error looking up IRQs for device\n", __func__);
		ret = -ENXIO;
		goto clk_unprep;
	}

	ret = request_irq(fb->irq, ltdc_interrupt, IRQF_SHARED, fb->pdev->name, fb);
	if (unlikely(ret < 0)) {
		pr_err("%s: error requesting IRQ %d\n", __func__, fb->irq);
		ret = -ENXIO;
		goto clk_unprep;
	}

	ret = request_irq(fb->error_irq, ltdc_interrupt, IRQF_SHARED, fb->pdev->name, fb);
	if (unlikely(ret < 0)) {
		pr_err("%s: error requesting error IRQ %d\n", __func__, fb->error_irq);
		ret = -ENXIO;
		goto clk_unprep;
	}

	fb->fb.screen_base = dma_alloc_wc(&fb->pdev->dev, framesize, &dma, GFP_KERNEL);
	if (!fb->fb.screen_base) {
		pr_err("%s: unable to allocate framebuffer\n", __func__);
		ret = -ENOMEM;
		goto clk_unprep;
	}

	{
		int x, y;
		u32 *data = (u32*)fb->fb.screen_base;
		for (y = 0; y < 128; y++)
			for (x = 0; x < 480; x++)
				data[y * 480 + x] = 0xffff0000;
	}

	fb->fb.fix.smem_start	= dma;
	fb->fb.fix.smem_len	= framesize;

	fb->fb.fbops		= &ltdcfb_ops;
	fb->fb.flags		= FBINFO_FLAG_DEFAULT;
	fb->fb.pseudo_palette	= fb->cmap;

	strncpy(fb->fb.fix.id, ltdc_name, sizeof(fb->fb.fix.id));
	fb->fb.fix.type		= FB_TYPE_PACKED_PIXELS;
	fb->fb.fix.type_aux	= 0;
	fb->fb.fix.xpanstep	= 0;
	fb->fb.fix.ypanstep	= 0;
	fb->fb.fix.ywrapstep	= 0;
	fb->fb.fix.accel	= FB_ACCEL_NONE;

	fb->panel->width	= 480;
	fb->panel->width	= 272;

	fb->fb.var.xres		= fb->panel->mode.xres;
	fb->fb.var.yres		= fb->panel->mode.yres;
	fb->fb.var.xres_virtual	= fb->panel->mode.xres;
	fb->fb.var.yres_virtual	= fb->panel->mode.yres;
	fb->fb.var.bits_per_pixel = fb->panel->bpp;
	fb->fb.var.grayscale	= fb->panel->grayscale;
	fb->fb.var.pixclock	= fb->panel->mode.pixclock;
	fb->fb.var.left_margin	= fb->panel->mode.left_margin;
	fb->fb.var.right_margin	= fb->panel->mode.right_margin;
	fb->fb.var.upper_margin	= fb->panel->mode.upper_margin;
	fb->fb.var.lower_margin	= fb->panel->mode.lower_margin;
	fb->fb.var.hsync_len	= fb->panel->mode.hsync_len;
	fb->fb.var.vsync_len	= fb->panel->mode.vsync_len;
	fb->fb.var.sync		= fb->panel->mode.sync;
	fb->fb.var.vmode	= fb->panel->mode.vmode;
	fb->fb.var.activate	= FB_ACTIVATE_NOW;
	fb->fb.var.nonstd	= 0;
	fb->fb.var.height	= fb->panel->height;
	fb->fb.var.width	= fb->panel->width;
	fb->fb.var.accel_flags	= 0;

	fb->fb.monspecs.hfmin	= 0;
	fb->fb.monspecs.hfmax   = 100000;
	fb->fb.monspecs.vfmin	= 0;
	fb->fb.monspecs.vfmax	= 400;
	fb->fb.monspecs.dclkmin = 1000000;
	fb->fb.monspecs.dclkmax	= 100000000;

	ltdcfb_set_bitfields(fb, &fb->fb.var);

	ret = fb_alloc_cmap(&fb->fb.cmap, 256, 0);
	if (ret)
		goto unmap;

	/* enable line + error interrupts */
	writel(0xf, fb->regs + LTDC_IER);

	writel((fb->panel->mode.vsync_len - 1) | ((fb->panel->mode.hsync_len - 1)  << 16),
	       fb->regs + LTDC_SSCR);
	writel((fb->panel->mode.vsync_len + fb->panel->mode.upper_margin - 1)
	       | ((fb->panel->mode.hsync_len + fb->panel->mode.left_margin - 1) << 16),
	       fb->regs + LTDC_BPCR);
	writel((fb->panel->mode.vsync_len + fb->panel->mode.upper_margin + 272 - 1)
	       | ((fb->panel->mode.hsync_len + fb->panel->mode.left_margin + 480 - 1) << 16),
	       fb->regs + LTDC_AWCR);
	writel((fb->panel->mode.vsync_len + fb->panel->mode.upper_margin + 272 + fb->panel->mode.lower_margin - 1)
	       | ((fb->panel->mode.hsync_len + fb->panel->mode.left_margin + 480 + fb->panel->mode.right_margin - 1) << 16),
	       fb->regs + LTDC_TWCR);

	/* enable the LTDC controller */
	writel(1, fb->regs + LTDC_GCR);

	/* enable the first layer */
	ltdcfb_enable(fb);

	fb_set_var(&fb->fb, &fb->fb.var);

	ltdcfb_set_start(fb);

	dev_info(&fb->pdev->dev, "%s display\n", fb->panel->mode.name);

	ret = register_framebuffer(&fb->fb);
	if (ret == 0)
		goto out;

	dev_info(&fb->pdev->dev, "cannot register framebuffer\n");

	fb_dealloc_cmap(&fb->fb.cmap);

	dma_free_wc(&fb->pdev->dev, fb->fb.fix.smem_len, fb->fb.screen_base,
		    fb->fb.fix.smem_start);
 unmap:
	iounmap(fb->regs);
 clk_unprep:
	clk_unprepare(fb->clk);
 free_clk:
	clk_put(fb->clk);
 out:
	return ret;
}

static int ltdcfb_of_get_dpi_panel_mode(struct device_node *node,
					struct fb_videomode *mode)
{
	int err;
	struct display_timing timing;
	struct videomode video;

	err = of_get_display_timing(node, "panel-timing", &timing);
	if (err)
		return err;

	videomode_from_timing(&timing, &video);

	err = fb_videomode_from_videomode(&video, mode);
	if (err)
		return err;

	return 0;
}

static int ltdcfb_snprintf_mode(char *buf, int size, struct fb_videomode *mode)
{
	return snprintf(buf, size, "%ux%u@%u", mode->xres, mode->yres,
			mode->refresh);
}

static int ltdcfb_of_get_mode(struct device *dev, struct device_node *endpoint,
			      struct fb_videomode *mode)
{
	int err;
	struct device_node *panel;
	char *name;
	int len;

	panel = of_graph_get_remote_port_parent(endpoint);
	if (!panel)
		return -ENODEV;

	/* Only directly connected DPI panels supported for now */
	if (of_device_is_compatible(panel, "panel-dpi"))
		err = ltdcfb_of_get_dpi_panel_mode(panel, mode);
	else
		err = -ENOENT;
	if (err)
		return err;

	len = ltdcfb_snprintf_mode(NULL, 0, mode);
	name = devm_kzalloc(dev, len + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	ltdcfb_snprintf_mode(name, len + 1, mode);
	mode->name = name;

	return 0;
}

static int ltdcfb_of_init(struct ltdc_fb *fb)
{
	struct device_node *endpoint;
	int err;
	unsigned int bpp;
	u32 max_bandwidth;

	fb->panel = devm_kzalloc(&fb->pdev->dev, sizeof(*fb->panel), GFP_KERNEL);
	if (!fb->panel)
		return -ENOMEM;

	endpoint = of_graph_get_next_endpoint(fb->pdev->dev.of_node, NULL);
	if (!endpoint)
		return -ENODEV;

	err = ltdcfb_of_get_mode(&fb->pdev->dev, endpoint, &fb->panel->mode);
	if (err)
		return err;

	err = of_property_read_u32(fb->pdev->dev.of_node, "max-memory-bandwidth",
				   &max_bandwidth);
	if (!err) {
		/*
		 * max_bandwidth is in bytes per second and pixclock in
		 * pico-seconds, so the maximum allowed bits per pixel is
		 *   8 * max_bandwidth / (PICOS2KHZ(pixclock) * 1000)
		 * Rearrange this calculation to avoid overflow and then ensure
		 * result is a valid format.
		 */
		bpp = max_bandwidth / (1000 / 8)
			/ PICOS2KHZ(fb->panel->mode.pixclock);
		bpp = rounddown_pow_of_two(bpp);
		if (bpp > 32)
			bpp = 32;
	} else
		bpp = 32;

	fb->panel->bpp = bpp;

	fb->panel->width = -1;
	fb->panel->height = -1;

	return 0;
}

static int __init ltdcfb_probe(struct platform_device *pdev)
{
	struct ltdc_fb *fb;
	int ret;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto out;

	fb = kzalloc(sizeof(struct ltdc_fb), GFP_KERNEL);
	if (!fb) {
		dev_err(&pdev->dev, "could not allocate new fb context\n");
		ret = -ENOMEM;
		goto free_region;
	}

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "could not find OF node\n");
		return -ENODEV;
	}

	fb->pdev = pdev;

	ret = ltdcfb_of_init(fb);
	if (ret) {
		dev_err(&pdev->dev, "could not initialize OF configuration\n");
		return -ENODEV;
	}

	ret = ltdcfb_register(fb);
	if (ret == 0) {
		dev_set_drvdata(&pdev->dev, fb);
		goto out;
	}

	dev_err(&pdev->dev, "failed probing Framebuffer device\n");

	/* error path */
	kfree(fb);
 free_region:
 out:
	return ret;
}

static int __exit ltdcfb_remove(struct platform_device *pdev)
{
	struct ltdc_fb *fb = dev_get_drvdata(&pdev->dev);

	ltdcfb_disable(fb);
	unregister_framebuffer(&fb->fb);
	if (fb->fb.cmap.len)
		fb_dealloc_cmap(&fb->fb.cmap);
	if (fb->fb.screen_base)
		dma_free_wc(&fb->pdev->dev, fb->fb.fix.smem_len, fb->fb.screen_base,
			    fb->fb.fix.smem_start);
	iounmap(fb->regs);
	clk_unprepare(fb->clk);
	clk_put(fb->clk);

	kfree(fb);

	return 0;
}

static const struct of_device_id ltdc_dt_ids[] = {
	{ .compatible = "st,ltdc" , NULL, }
};

MODULE_DEVICE_TABLE(of, ltdc_dt_ids);

static struct platform_driver ltdcfb_driver = {
	.remove		= __exit_p(ltdcfb_remove),
	.driver		= {
		.name	= "LTDC FB",
		.of_match_table	= of_match_ptr(ltdc_dt_ids),
	},
};

module_platform_driver_probe(ltdcfb_driver, ltdcfb_probe);

MODULE_DESCRIPTION("STM32 LTDC Framebuffer driver");
MODULE_AUTHOR("Ilyes Gouta <ilyes.gouta@gmail.com>");
MODULE_LICENSE("GPL");
