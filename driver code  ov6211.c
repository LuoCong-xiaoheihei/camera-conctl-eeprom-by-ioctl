      
From 472d1ea9f3be094971fb6e71b244fc10d5bc5e02 Mon Sep 17 00:00:00 2001
From: Li Xiaodong <xiaodong.li@thundersoft.com>
Date: Sat, 24 Dec 2022 19:13:03 +0800
Subject: [PATCH] [BSP]:[EEPROM]:Eeprom char device driver develop complete

Complete eeprom char device driver development, implement ioctl interface
for writing and reading eeprom.

Change-Id: If5a26aee4fb592221078271be9acb5bdd8bb3c96
---

diff --git a/drivers/media/platform/exynos/camera/sensor/module_framework/cis/is-cis-ov6211.c b/drivers/media/platform/exynos/camera/sensor/module_framework/cis/is-cis-ov6211.c
index 073eb7a..277d893 100755
--- a/drivers/media/platform/exynos/camera/sensor/module_framework/cis/is-cis-ov6211.c
+++ b/drivers/media/platform/exynos/camera/sensor/module_framework/cis/is-cis-ov6211.c
@@ -823,7 +823,7 @@
 		ret = hold;
 		goto p_err;
 	}
-	
+
 	/* Short exposure */
 	ret = is_sensor_write8(client, OV6211_AEC_EXPO_0, (short_coarse_int & 0xf000) >> 12 );
 	if (ret < 0)
@@ -1462,6 +1462,268 @@
 	.cis_wait_streamon = sensor_cis_wait_streamon,
 	.cis_check_rev_on_init = sensor_ov6211_cis_check_rev,
 };
+
+#define FM24C32_READ_DATA_MEMORY       _IOR('c',0,unsigned int)   //读DATA_MEMORY,操作地址范围0x0 ~ 0xfff
+#define FM24C32_READ_SECURITY_SECTOR   _IOR('c',1,unsigned int)   //读SECUNTY_SECTOR,操作地址范围0x0 ~ 0x1f
+#define FM24C32_WRITE_DATA_MEMORY      _IOW('c',0,unsigned int)   //写DATA_MEMORY,操作地址范围0x0 ~ 0xfff
+#define FM24C32_WRITE_SECURITY_SECTOR  _IOW('c',1,unsigned int)   //写SECUNTY_SECTOR,操作地址范围0x0 ~ 0x1f
+#define USER_CUSTON_ACTION             _IOR('c',2,unsigned int)   //用户自由读写,根据用户需求读写
+#define FM24C32_READ_UNIQUE_ID         _IOR('c',3,unsigned int)   //读设备id，地址固定为0x10
+
+enum fm24c32_device {
+	LEFT_FM24C32,   //左芯片
+	RIGHT_FM24C32,  //右芯片
+};
+
+enum operation {
+	READ_FM24C32,   //读操作
+	WRITE_FM24C32,  //写操作
+};
+
+typedef struct {
+	unsigned int len;         //传输数据长度
+	unsigned short addr;      //读写起始地址
+	unsigned char *buf;       //数据传输buf
+	enum fm24c32_device device; //具体的设备，左或右
+	enum operation m_operation; //读写类型
+	unsigned int fm24c32_slave_addr; //从机地址，
+				//不是USER_CUSTON_ACTION时此参数无效
+} fm24c32_userdata_t;
+
+struct eyetrack_e2prom {
+	dev_t dev_num;
+	struct is_cis *cis;
+	struct cdev chrdev;
+	struct class *fm24c32_class;
+	struct device *fm24c32_device;
+};
+
+static struct eyetrack_e2prom s_eyetrack_e2prom_dev;
+
+int eyetrack_e2prom_open(struct inode *node, struct file *fp)
+{
+	return 0;
+}
+
+long eyetrack_e2prom_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
+{
+	int i, ret;
+	short addr;
+	short device_addr;
+	unsigned int len;
+	unsigned int slave_addr;
+	unsigned char *buf = NULL;
+	enum operation m_operation;
+	enum fm24c32_device opt_dev;
+	fm24c32_userdata_t fm24c32_userdata;
+	struct i2c_client *client = s_eyetrack_e2prom_dev.cis->client;
+
+	if (!s_eyetrack_e2prom_dev.cis
+		|| !s_eyetrack_e2prom_dev.cis->cis_data
+		|| !s_eyetrack_e2prom_dev.cis->cis_data->stream_on) {
+		pr_err("<%s %d> camera not open\n", __func__, __LINE__);
+		return -1;
+	}
+
+	if (copy_from_user(&fm24c32_userdata, (void __user *)arg, sizeof(fm24c32_userdata)))
+		return -EFAULT;
+
+	len = (0 == fm24c32_userdata.len) ? 32 : fm24c32_userdata.len;
+	addr = fm24c32_userdata.addr;
+	opt_dev = fm24c32_userdata.device;
+	m_operation = fm24c32_userdata.m_operation;
+	slave_addr = fm24c32_userdata.fm24c32_slave_addr;
+
+	buf = kzalloc(len, GFP_KERNEL | GFP_DMA);
+	if (!buf) {
+		pr_err("<%s %d> failed to kzalloc\n", __func__, __LINE__);
+		return -EFAULT;
+	}
+
+	pr_debug("<%s %d> len = %d, addr = 0x%04x, opt_dev = %d, m_operation = %d, slave_addr = 0x%04x\n",
+		__func__, __LINE__, len, addr, opt_dev, m_operation, slave_addr);
+
+	I2C_MUTEX_LOCK(s_eyetrack_e2prom_dev.cis->i2c_lock);
+	client->addr = SENSOR_MAX7366_SLAVE_ADDR >> 1;
+	if (LEFT_FM24C32 == opt_dev) {
+		ret = is_sensor_write16(client, 0x0006, 0x0001);
+		if (ret < 0) {
+			pr_err("<%s %d> failed to routed to left device\n", __func__, __LINE__);
+			goto exit;
+		}
+	} else {
+		ret = is_sensor_write16(client, 0x0006, 0x0002);
+		if (ret < 0) {
+			pr_err("<%s %d> failed to routed to right device\n", __func__, __LINE__);
+			goto exit;
+		}
+	}
+
+	switch(cmd) {
+		case FM24C32_READ_DATA_MEMORY:
+			ret = is_sensor_write16(client, 0x0004, 0xa0a0);
+			if (ret < 0) {
+				pr_err("<%s %d> failed to config max7366 device addr\n", __func__, __LINE__);
+				goto exit;
+			}
+
+			client->addr = SENSOR_EEPROM_DATA_SLAVE_ADDR >> 1;
+			for (i = 0; i < len; i++) {
+				msleep(1);
+				ret = is_sensor_read8(client, addr, &buf[i]);
+				if (ret) {
+					pr_err("<%s %d> failed to read data memory, addr 0x0000, len = %d, ret = %d\n",
+						__func__, __LINE__, len, ret);
+					ret = -1;
+					goto exit;
+				}
+			}
+			copy_to_user(fm24c32_userdata.buf, buf, len);
+			break;
+		case FM24C32_READ_SECURITY_SECTOR:
+			ret = is_sensor_write16(client, 0x0004, 0xb0b0);
+			if (ret < 0) {
+				pr_err("<%s %d> failed to config max7366 device addr\n", __func__, __LINE__);
+				goto exit;
+			}
+
+			client->addr = SENSOR_EEPROM_UNIQUE_SLAVE_ADDR >> 1;
+			for (i = 0; i < 32; i++) {
+				msleep(1);
+				ret = is_sensor_read8(client, addr, &buf[i]);
+				if (ret) {
+					pr_err("<%s %d> failed to read security sector, addr 0x%04x, ret = %d\n",
+						__func__, __LINE__, addr, ret);
+					ret = -1;
+					goto exit;
+				}
+				addr++;
+			}
+			copy_to_user(fm24c32_userdata.buf, buf, len);
+			break;
+		case FM24C32_WRITE_DATA_MEMORY:
+			ret = is_sensor_write16(client, 0x0004, 0xa0a0);
+			if (ret < 0) {
+				pr_err("<%s %d> failed to config max7366 device addr\n", __func__, __LINE__);
+				goto exit;
+			}
+
+			client->addr = SENSOR_EEPROM_DATA_SLAVE_ADDR >> 1;
+			copy_from_user(buf, fm24c32_userdata.buf, len);
+			for (i = 0; i < len; i++) {
+				msleep(1);
+				ret = is_sensor_write8(client, addr, buf[i]);
+				if (ret) {
+					pr_err("<%s %d> failed to write data memory, addr 0x%04x, val 0x%02x, ret = %d\n",
+						__func__, __LINE__, addr, buf[i], ret);
+					goto exit;
+				}
+				addr++;
+			}
+			break;
+		case FM24C32_WRITE_SECURITY_SECTOR:
+			ret = is_sensor_write16(client, 0x0004, 0xb0b0);
+			if (ret < 0) {
+				pr_err("<%s %d> failed to config max7366 device addr\n", __func__, __LINE__);
+				goto exit;
+			}
+
+			client->addr = SENSOR_EEPROM_UNIQUE_SLAVE_ADDR >> 1;
+			copy_from_user(buf, fm24c32_userdata.buf, len);
+			for (i = 0; i < len; i++) {
+				msleep(1);
+				ret = is_sensor_write8(client, addr, buf[i]);
+				if (ret) {
+					pr_err("<%s %d> failed to write addr 0x%04x, val 0x%02x\n",
+						__func__, __LINE__, addr, buf[i]);
+					goto exit;
+				}
+				addr++;
+			}
+			break;
+		case USER_CUSTON_ACTION:
+			device_addr = ((slave_addr & 0xff) << 8) | (slave_addr & 0xff);
+			ret = is_sensor_write16(client, 0x0004, device_addr);
+			if (ret < 0) {
+				pr_err("<%s %d> failed to config max7366 device addr\n", __func__, __LINE__);
+				goto exit;
+			}
+
+			client->addr = (slave_addr & 0xff) >> 1;
+			if (READ_FM24C32 == m_operation) {
+				for (i = 0; i < len; i++) {
+					msleep(1);
+					ret = is_sensor_read8(client, addr, &buf[i]);
+					if (ret) {
+						pr_err("<%s %d> failed to read, addr 0x0000, ret = %d\n",
+							__func__, __LINE__, ret);
+						ret = -1;
+						goto exit;
+					}
+					addr++;
+				}
+				copy_to_user(fm24c32_userdata.buf, buf, len);
+			} else {
+				copy_from_user(buf, fm24c32_userdata.buf, len);
+				for (i = 0; i < len; i++) {
+					msleep(1);
+					ret = is_sensor_write8(client, addr, buf[i]);
+					if (ret) {
+						pr_err("<%s %d> failed to write, slave_addr 0x%04x, addr 0x%04x, val 0x%02x, ret = %d\n",
+							__func__, __LINE__, (slave_addr & 0xff), addr, buf[i], ret);
+						goto exit;
+					}
+					addr++;
+				}
+			}
+			break;
+		case FM24C32_READ_UNIQUE_ID:
+			ret = is_sensor_write16(client, 0x0004, 0xb0b0);
+			if (ret < 0) {
+				pr_err("<%s %d> failed to config max7366 device addr\n",
+					__func__, __LINE__);
+				goto exit;
+			}
+
+			client->addr = SENSOR_EEPROM_UNIQUE_SLAVE_ADDR >> 1;
+			addr = 0x0200;
+			for (i = 0; i < 16; i++) {
+				msleep(1);
+				ret = is_sensor_read8(client, addr, &buf[i]);
+				if (ret) {
+					pr_err("<%s %d> failed to read unique id, addr 0x%04x, ret = %d\n",
+						__func__, __LINE__, addr, ret);
+					ret = -1;
+					goto exit;
+				}
+				addr++;
+			}
+			copy_to_user(fm24c32_userdata.buf, buf, 16);
+			break;
+		default:
+			break;
+	}
+
+	ret = 0;
+
+exit:
+	I2C_MUTEX_UNLOCK(s_eyetrack_e2prom_dev.cis->i2c_lock);
+	kfree(buf);
+	return ret;
+}
+
+int eyetrack_e2prom_release(struct inode *node, struct file *fp)
+{
+	return 0;
+}
+
+const struct file_operations eyetrack_e2prom_ops = {
+	.owner		= THIS_MODULE,
+	.open       = eyetrack_e2prom_open,
+	.release	= eyetrack_e2prom_release,
+	.unlocked_ioctl	= eyetrack_e2prom_ioctl,
+};
+
 static int cis_ov6211_probe(struct i2c_client *client,
 	const struct i2c_device_id *id)
 {
@@ -1540,8 +1802,42 @@
 		}
 	}
 	init_bsp_sysfs();
-	probe_info("%s done\n", __func__);
-	return ret;
+	probe_info("%s 1 done\n", __func__);
+
+	// add char device driver
+	ret = alloc_chrdev_region(&s_eyetrack_e2prom_dev.dev_num, 0, 1, "e2prom");
+	if (ret) {
+		err("%s alloc chrdev num failed", __func__);
+		return ret;
+	}
+
+	cdev_init(&s_eyetrack_e2prom_dev.chrdev, &eyetrack_e2prom_ops);
+
+	ret = cdev_add(&s_eyetrack_e2prom_dev.chrdev, s_eyetrack_e2prom_dev.dev_num, 1);
+	if (ret) {
+		unregister_chrdev_region(s_eyetrack_e2prom_dev.dev_num, 1);
+		err("%s add chrdev failed", __func__);
+		return ret;
+	}
+
+	s_eyetrack_e2prom_dev.fm24c32_class = class_create(THIS_MODULE, "e2prom");
+	if (IS_ERR(s_eyetrack_e2prom_dev.fm24c32_class)) {
+		unregister_chrdev_region(s_eyetrack_e2prom_dev.dev_num, 1);
+		return PTR_ERR(s_eyetrack_e2prom_dev.fm24c32_class);
+	}
+
+	s_eyetrack_e2prom_dev.fm24c32_device = device_create(s_eyetrack_e2prom_dev.fm24c32_class, NULL,
+		s_eyetrack_e2prom_dev.dev_num, NULL, "fm24c32");
+	if (IS_ERR(s_eyetrack_e2prom_dev.fm24c32_device)) {
+		unregister_chrdev_region(s_eyetrack_e2prom_dev.dev_num, 1);
+		return PTR_ERR(s_eyetrack_e2prom_dev.fm24c32_device);
+	}
+
+	s_eyetrack_e2prom_dev.cis = cis;
+
+	probe_info("%s 2 done\n", __func__);
+
+	return 0;
 }
 static const struct of_device_id sensor_cis_ov6211_match[] = {
 	{

    
