/*
 * eeprom.c - Crenova version to read MAC address
 *
 * (c) 2011 konfetti
 * partly copied from uboot source!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * Description:
 *
 * Little utility to dump the register file present in many Crenova
 * receivers. The register file consists of 128 8 byte registers.
 * In effect this is a stripped down and modified version of
 * ipbox_eeprom, kudos to the original authors.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define CFG_EEPROM_ADDR  0x3d
#define CFG_EEPROM_SIZE  128

//#define EEPROM_WRITE
#if defined EEPROM_WRITE
#define CFG_EEPROM_PAGE_WRITE_DELAY_MS	11	/* 10ms. but give more */
#define CFG_EEPROM_PAGE_WRITE_BITS 4
#define	EEPROM_PAGE_SIZE  (1 << CFG_EEPROM_PAGE_WRITE_BITS)
#endif

/* ********************************* i2c ************************************ */
/*
 * I2C Message - used for pure i2c transaction, also from /dev interface
 */
struct i2c_msg
{
	unsigned short addr;    /* slave address       */
	unsigned short flags;
	unsigned short len;     /* msg length          */
	unsigned char  *buf;    /* pointer to msg data */
};

/* This is the structure as used in the I2C_RDWR ioctl call */
struct i2c_rdwr_ioctl_data
{
	struct i2c_msg *msgs;   /* pointers to i2c_msgs */
	unsigned int nmsgs;     /* number of i2c_msgs   */
};


#define I2C_SLAVE 0x0703    /* Change slave address */
/* Attn.: Slave address is 7 or 10 bits */

#define I2C_RDWR 0x0707     /* Combined R/W transfer (one stop only) */

int i2c_read(int fd_i2c, unsigned char addr, unsigned char reg, unsigned char *buffer, int len)
{
	struct i2c_rdwr_ioctl_data i2c_rdwr;
	int	err;
	unsigned char b0[] = { reg };

	//printf("%s > i2c addr = 0x%02x, reg = 0x%02x, len = %d\n", __func__, addr, reg, len);

	i2c_rdwr.nmsgs = 2;
	i2c_rdwr.msgs = malloc(2 * sizeof(struct i2c_msg));
	i2c_rdwr.msgs[0].addr = addr;  // set register number
	i2c_rdwr.msgs[0].flags = 0;
	i2c_rdwr.msgs[0].len = 1;
	i2c_rdwr.msgs[0].buf = b0;

	i2c_rdwr.msgs[1].addr = addr;  // read len bytes
	i2c_rdwr.msgs[1].flags = 1;
	i2c_rdwr.msgs[1].len = len;
	i2c_rdwr.msgs[1].buf = malloc(len);

	memset(i2c_rdwr.msgs[1].buf, 0, len);

	if ((err = ioctl(fd_i2c, I2C_RDWR, &i2c_rdwr)) < 0)
	{
		printf("[eeprom] %s i2c_read of reg 0x%02x failed %d %d\n", __func__, reg, err, errno);
		printf("         %s\n", strerror(errno));
		free(i2c_rdwr.msgs[0].buf);
		free(i2c_rdwr.msgs);
		return -1;
	}

	//printf("[eeprom] %s reg 0x%02x -> ret 0x%02x\n", __func__, reg, (i2c_rdwr.msgs[1].buf[0] & 0xff));
	memcpy(buffer, i2c_rdwr.msgs[1].buf, len);

	free(i2c_rdwr.msgs[1].buf);
	free(i2c_rdwr.msgs);

	//printf("[eeprom] %s <\n", __func__);
	return 0;
}

#if defined EEPROM_WRITE
int i2c_write(int fd_i2c, unsigned char addr, unsigned char reg, unsigned char *buffer, int len)
{
	struct i2c_rdwr_ioctl_data i2c_rdwr;
	int	err;
	unsigned char buf[256];

	//printf("%s > 0x%0x - %s - %d\n", __func__, reg, buffer, len);

	buf[0] = reg;
//	memcpy(&buf[1], buffer, len);

	i2c_rdwr.nmsgs = 1;
	i2c_rdwr.msgs = malloc(1 * sizeof(struct i2c_msg));
	i2c_rdwr.msgs[0].addr = addr;
	i2c_rdwr.msgs[0].flags = 0;
	i2c_rdwr.msgs[0].len = 1;
	i2c_rdwr.msgs[0].buf = buf;

	if ((err = ioctl(fd_i2c, I2C_RDWR, &i2c_rdwr)) < 0)
	{
		//printf("i2c_write failed %d %d\n", err, errno);
		//printf("%s\n", strerror(errno));
		free(i2c_rdwr.msgs[0].buf);
		free(i2c_rdwr.msgs);
		return -1;
	}
	free(i2c_rdwr.msgs);
//	printf("%s <\n", __func__);
	return 0;
}
#endif

/* *************************** uboot copied func ************************************** */

#if defined EEPROM_WRITE
int eeprom_write(int fd, unsigned dev_addr, unsigned offset, unsigned char *buffer, unsigned cnt)
{
	unsigned end = offset + cnt;
	unsigned blk_off;
	int rcode = 0;

	/* Write data until done or would cross a write page boundary.
	 * We must write the address again when changing pages
	 * because the address counter only increments within a page.
	 */

	printf("[eeprom] %s >\n", __func__);
	while (offset < end)
	{
		unsigned len;
		unsigned char addr[2];

		blk_off = offset & 0xFF;    /* block offset */

		addr[0] = offset >> 8;      /* block number */
		addr[1] = blk_off;          /* block offset */
		addr[0] |= dev_addr;        /* insert device address */

		len = end - offset;

		if (i2c_write(fd, addr[0], offset, buffer, len) != 0)
		{
			rcode = 1;
		}
		buffer += len;
		offset += len;

		usleep(CFG_EEPROM_PAGE_WRITE_DELAY_MS * 1000);
	}
	printf("[eeprom] %s < (%d)\n", __func__, rcode);
	return rcode;
}
#endif

#if 0
/* Read one register */
int eeprom_read(int fd, unsigned dev_addr, unsigned reg_num, unsigned char *buffer, int len)
{
//	unsigned end = offset + cnt;
//	unsigned blk_off;
	int rcode = 0;
//	int i;

	printf("[eeprom] %s > register 0x%03x\n", __func__, reg_num);

	rcode = i2c_read(fd, dev_addr, reg_num, buffer, len);
	//printf("[eeprom] %s < (%d)\n", __func__, rcode);
	return rcode;
}
#endif

int main(int argc, char *argv[])
{
	FILE *boxtype;
	char *buffer = NULL;
	int fd_i2c;
	int vLoop;

//	printf("%s >\n", argv[0]);

	// determine box model
	boxtype = fopen("/proc/stb/info/boxtype", "r");
	if (boxtype == NULL)
	{
		printf("Cannot determine boxtype, using i2c bus 0.\n");
		fd_i2c = open("/dev/i2c-0", O_RDWR);
	}
	else
	{
		buffer = malloc(CFG_EEPROM_SIZE);
		fscanf(boxtype, "%s", buffer);
		fclose(boxtype);
		printf("Boxtype is %s.\n", buffer);

		if ((strncmp(buffer, "opt9600mini", 11) == 0)
		||  (strncmp(buffer, "opt9600prima", 12) == 0)
		||  (strncmp(buffer, "atemio520", 9) == 0))
		{
//			printf("Using I2C bus 1.\n");
			fd_i2c = open("/dev/i2c-1", O_RDWR);
		}
		else if (strncmp(buffer, "opt9600", 7) == 0)
		{
//			printf("Using I2C bus 0.\n");
			fd_i2c = open("/dev/i2c-0", O_RDWR);
		}
		else
		{
//			printf("Using I2C bus 2.\n");
			fd_i2c = open("/dev/i2c-2", O_RDWR);
		}
	}

	if (argc == 2 )  // 1 argument given
	{
		int i, rcode = 0;

		if ((strcmp(argv[1], "-m") == 0) || (strcmp(argv[1], "--mac") == 0))
		{
			unsigned char buf[8];
			unsigned char mac[6];

			// The MAC is constructed using the prefix used by Opticum for the 1st three bytes
			// and the last three bytes of the serial number stored in the front processor
#if 0
			rcode = i2c_read(fd_i2c, CFG_EEPROM_ADDR, 0x75, buf, 8);  // read model code

			if (rcode < 0)
			{
				rcode = 1;
				goto failed;
			}

			mac[0] = buf[5];
			mac[1] = buf[6];
			mac[2] = buf[7];
#else
			mac[0] = 0x00;
			mac[1] = 0x25;
			mac[2] = 0xff;
#endif
			rcode = i2c_read(fd_i2c, CFG_EEPROM_ADDR, 0x76, buf, 8);  // read serial number

			if (rcode < 0)
			{
				rcode = 1;
				goto failed;
			}

			mac[3] = buf[5];
			mac[4] = buf[6];
			mac[5] = buf[7];

			for (vLoop = 0; vLoop < 6; vLoop++)
			{
				printf("%02x", mac[vLoop]);
				if (vLoop != 5)
				{
					printf(":");
				}
			}
			printf("\n");
		}
#if 0
		else if ((strcmp(argv[1], "-d") == 0) || (strcmp(argv[1], "--dump") == 0))
		{
			unsigned char buf[CFG_EEPROM_SIZE * 8];

			for (vLoop = 0; vLoop < CFG_EEPROM_SIZE; vLoop++)
			{
				rcode |= i2c_read(fd_i2c, CFG_EEPROM_ADDR, vLoop, buf + (vLoop * 8), 8);
			}
			if (rcode < 0)
			{
				rcode = 1;
				goto failed;
			}
			printf("Register file dump\n");
			printf("Reg   0  1  2  3  4  5  6  7  ASCII\n");
			printf("--------------------------------------\n");
			for (vLoop = 0; vLoop < CFG_EEPROM_SIZE; vLoop+= 8)
			{
				printf(" %02x ", vLoop);  // register number
				for (i = 0; i < 8; i++)
				{
					printf(" %02x", buf[vLoop + i]);
				}
				printf("  ");
				for (i = 0; i < 8; i++)
				{
					if (buf[vLoop + i] < 0x20 
					||  buf[vLoop + i] > 0x7e)
					{
						printf(".");
					}
					else
					{
						printf("%c", buf[vLoop + i]);
					}
				}
				printf("\n");
			}
		}
#endif
	}
	else  // no arguments; show usage
	{
		printf("%s version 1.0\n\n", argv[0]);
		printf(" Usage:\n");
		printf("%s [ -d | --dump | -m | --mac ]\n\n", argv[0]);
		printf(" no args    : show this usage\n");
//		printf(" -d | --dump: hex dump eeprom contents\n");
		printf(" -m | --mac : show MAC address\n");
	}
	return 0;

failed:
	printf("[eeprom] failed <\n");
	return -1;
}
// vim:ts=4
