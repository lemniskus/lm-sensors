/*
    proc.c - Part of libsensors, a Linux library for reading sensor data.
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include "kernel/include/sensors.h"
#include "data.h"
#include "error.h"
#include "access.h"
#include "general.h"
#include <limits.h>
#include <dirent.h>

/* OK, this proves one thing: if there are too many chips detected, we get in
   trouble. The limit is around 4096/sizeof(struct sensors_chip_data), which
   works out to about 100 entries right now. That seems sensible enough,
   but if we ever get at the point where more chips can be detected, we must
   enlarge buf, and check that sysctl can handle larger buffers. */

#define BUF_LEN 4096

static char buf[BUF_LEN];

sensors_proc_chips_entry *sensors_proc_chips;
int sensors_proc_chips_count, sensors_proc_chips_max;
int sensors_sys_chips_count, sensors_sys_chips_max;

sensors_bus *sensors_sys_bus;
int sensors_sys_bus_count, sensors_sys_bus_max;
sensors_bus *sensors_proc_bus;
int sensors_proc_bus_count, sensors_proc_bus_max;

static int sensors_get_chip_id(sensors_chip_name name);

int foundsysfs=0;
char sysfsmount[NAME_MAX];

#define add_proc_chips(el) sensors_add_array_el(el,\
                                       (void **) &sensors_proc_chips,\
                                       &sensors_proc_chips_count,\
                                       &sensors_proc_chips_max,\
                                       sizeof(struct sensors_proc_chips_entry))

#define add_proc_bus(el) sensors_add_array_el(el,\
                                       (void **) &sensors_proc_bus,\
                                       &sensors_proc_bus_count,\
                                       &sensors_proc_bus_max,\
                                       sizeof(struct sensors_bus))

int getsysname(const sensors_chip_feature *feature, char *sysname, int *sysmag);

/* This reads /proc/sys/dev/sensors/chips into memory */
int sensors_read_proc_chips(void)
{
	struct dirent *de;
	DIR *dir;
	FILE *f;
	char dev[NAME_MAX], fstype[NAME_MAX], sysfs[NAME_MAX], n[NAME_MAX];
	char dirname[NAME_MAX];
	int res;

	int name[3] = { CTL_DEV, DEV_SENSORS, SENSORS_CHIPS };
	int buflen = BUF_LEN;
	char *bufptr = buf;
	sensors_proc_chips_entry entry;
	int lineno;

	/* First figure out where sysfs was mounted */
	if ((f = fopen("/proc/mounts", "r")) == NULL)
		goto proc;
	while (fgets(n, NAME_MAX, f)) {
		sscanf(n, "%[^ ] %[^ ] %[^ ] %*s\n", dev, sysfs, fstype);
		if (strcasecmp(fstype, "sysfs") == 0) {
			foundsysfs++;
			break;
		}
	}
	fclose(f);
	if (! foundsysfs)
		goto proc;
	strcpy(sysfsmount, sysfs);
	strcat(sysfs, "/bus/i2c/devices");

	/* Then read from it */
	dir = opendir(sysfs);
	if (! dir)
		goto proc;

	while ((de = readdir(dir)) != NULL) {
		if (!strcmp(de->d_name, "."))
			continue;
		if (!strcmp(de->d_name, ".."))
			continue;
/*
		if (de->d_type != DT_DIR && de->d_type != DT_LNK)
			continue;
*/

		sprintf(n, "%s/%s", sysfs, de->d_name);
		strcpy(dirname, n);
		strcat(n, "/name");

		if ((f = fopen(n, "r")) != NULL) {
			char	x[120];
			fscanf(f, "%[a-zA-z0-9_]", x);
			fclose(f);
			/* HACK */ strcat(x, "-*");
			if ((res = sensors_parse_chip_name(x, &entry.name))) {
				char	em[NAME_MAX + 20];
				strcpy(em, "Parsing ");
				strcat(em, n);
				sensors_parse_error(em, 0);
				return res;
			}
			entry.name.busname = strdup(dirname);
			sscanf(de->d_name, "%d-%x", &entry.name.bus, &entry.name.addr);
			/* find out if ISA or not */
			sprintf(n, "%s/class/i2c-adapter/i2c-%d/device/name",
			        sysfsmount, entry.name.bus);
			if ((f = fopen(n, "r")) != NULL) {
				fgets(x, 5, f);
				fclose(f);
				if(!strncmp(x, "ISA ", 4))
					entry.name.bus = SENSORS_CHIP_NAME_BUS_ISA;
			}
			add_proc_chips(&entry);
		}
	}
	closedir(dir);
	return 0;

proc:

  if (sysctl(name, 3, bufptr, &buflen, NULL, 0))
    return -SENSORS_ERR_PROC;

  lineno = 1;
  while (buflen >= sizeof(struct i2c_chips_data)) {
    if ((res = 
          sensors_parse_chip_name(((struct i2c_chips_data *) bufptr)->name, 
                                   &entry.name))) {
      sensors_parse_error("Parsing /proc/sys/dev/sensors/chips",lineno);
      return res;
    }
    entry.sysctl = ((struct i2c_chips_data *) bufptr)->sysctl_id;
    add_proc_chips(&entry);
    bufptr += sizeof(struct i2c_chips_data);
    buflen -= sizeof(struct i2c_chips_data);
    lineno++;
  }
  return 0;
}

int sensors_read_proc_bus(void)
{
	struct dirent *de;
	DIR *dir;
	FILE *f;
	char line[255];
	char *border;
	sensors_bus entry;
	int lineno;
	char sysfs[NAME_MAX], n[NAME_MAX];
	char dirname[NAME_MAX];

	if(foundsysfs) {
		strcpy(sysfs, sysfsmount);
		strcat(sysfs, "/class/i2c-adapter");
		/* Then read from it */
		dir = opendir(sysfs);
		if (! dir)
			goto proc;

		while ((de = readdir(dir)) != NULL) {
			if (!strcmp(de->d_name, "."))
				continue;
			if (!strcmp(de->d_name, ".."))
				continue;

			strcpy(n, sysfs);
			strcat(n, "/");
			strcat(n, de->d_name);
			strcpy(dirname, n);
			strcat(n, "/device/name");

			if ((f = fopen(n, "r")) != NULL) {
				char	x[120];
				fgets(x, 120, f);
				fclose(f);
				if((border = index(x, '\n')) != NULL)
					*border = 0;
				entry.adapter=strdup(x);
				if(!strncmp(x, "ISA ", 4)) {
					entry.number = SENSORS_CHIP_NAME_BUS_ISA;
					entry.algorithm = "ISA bus algorithm";
				} else if(!sscanf(de->d_name, "i2c-%d", &entry.number)) {
					entry.number = SENSORS_CHIP_NAME_BUS_DUMMY;
					entry.algorithm = "Dummy bus algorithm";
				} else
					entry.algorithm = "Unavailable from sysfs";
				add_proc_bus(&entry);
			}
		}
		closedir(dir);
		return 0;
	}

proc:

  f = fopen("/proc/bus/i2c","r");
  if (!f)
    return -SENSORS_ERR_PROC;
  lineno=1;
  while (fgets(line,255,f)) {
    if (strlen(line) > 0)
      line[strlen(line)-1] = '\0';
    if (! (border = rindex(line,'\t')))
      goto ERROR;
    if (! (entry.algorithm = strdup(border+1)))
      goto FAT_ERROR;
    *border='\0';
    if (! (border = rindex(line,'\t')))
      goto ERROR;
    if (! (entry.adapter = strdup(border + 1)))
      goto FAT_ERROR;
    *border='\0';
    if (! (border = rindex(line,'\t')))
      goto ERROR;
    *border='\0';
    if (strncmp(line,"i2c-",4))
      goto ERROR;
    if (sensors_parse_i2cbus_name(line,&entry.number))
      goto ERROR;
    sensors_strip_of_spaces(entry.algorithm);
    sensors_strip_of_spaces(entry.adapter);
    add_proc_bus(&entry);
    lineno++;
  }
  fclose(f);
  return 0;
FAT_ERROR:
  sensors_fatal_error("sensors_read_proc_bus","Allocating entry");
ERROR:
  sensors_parse_error("Parsing /proc/bus/i2c",lineno);
  fclose(f);
  return -SENSORS_ERR_PROC;
}
    

/* This returns the first detected chip which matches the name */
int sensors_get_chip_id(sensors_chip_name name)
{
  int i;
  for (i = 0; i < sensors_proc_chips_count; i++)
    if (sensors_match_chip(name, sensors_proc_chips[i].name))
      return sensors_proc_chips[i].sysctl;
  return -SENSORS_ERR_NO_ENTRY;
}
  
/* This reads a feature /proc file */
int sensors_read_proc(sensors_chip_name name, int feature, double *value)
{
	int sysctl_name[4] = { CTL_DEV, DEV_SENSORS };
	const sensors_chip_feature *the_feature;
	int buflen = BUF_LEN;
	int mag;
	char n[NAME_MAX];
	FILE *f;

	if(!foundsysfs)
		if ((sysctl_name[2] = sensors_get_chip_id(name)) < 0)
			return sysctl_name[2];
	if (! (the_feature = sensors_lookup_feature_nr(name.prefix,feature)))
		return -SENSORS_ERR_NO_ENTRY;
	if(foundsysfs) {
		strcpy(n, name.busname);
		strcat(n, "/");
		/* use rindex to append sysname to n */
		getsysname(the_feature, rindex(n, '\0'), &mag);
		if ((f = fopen(n, "r")) != NULL) {
			fscanf(f, "%lf", value);
			fclose(f);
			for (; mag > 0; mag --)
				*value /= 10.0;
	//		fprintf(stderr, "Feature %s value %lf scale %d offset %d\n",
	//			the_feature->name, *value,
	//			the_feature->scaling, the_feature->offset);
			return 0;
		} else
			return -SENSORS_ERR_PROC;
	} else {
		sysctl_name[3] = the_feature->sysctl;
		if (sysctl(sysctl_name, 4, buf, &buflen, NULL, 0))
			return -SENSORS_ERR_PROC;
		*value = *((long *) (buf + the_feature->offset));
		for (mag = the_feature->scaling; mag > 0; mag --)
			*value /= 10.0;
		for (; mag < 0; mag ++)
			*value *= 10.0;
	}
	return 0;
}
  
int sensors_write_proc(sensors_chip_name name, int feature, double value)
{
	int sysctl_name[4] = { CTL_DEV, DEV_SENSORS };
	const sensors_chip_feature *the_feature;
	int buflen = BUF_LEN;
	int mag;
	char n[NAME_MAX];
	FILE *f;
 
	if(!foundsysfs)
		if ((sysctl_name[2] = sensors_get_chip_id(name)) < 0)
			return sysctl_name[2];
	if (! (the_feature = sensors_lookup_feature_nr(name.prefix,feature)))
		return -SENSORS_ERR_NO_ENTRY;
	if(foundsysfs) {
		strcpy(n, name.busname);
		strcat(n, "/");
		/* use rindex to append sysname to n */
		getsysname(the_feature, rindex(n, '\0'), &mag);
		if ((f = fopen(n, "w")) != NULL) {
			for (; mag > 0; mag --)
				value *= 10.0;
			fprintf(f, "%d", (int) value);
			fclose(f);
		} else
			return -SENSORS_ERR_PROC;
	} else {
		sysctl_name[3] = the_feature->sysctl;
		if (sysctl(sysctl_name, 4, buf, &buflen, NULL, 0))
			return -SENSORS_ERR_PROC;
		if (sysctl_name[0] != CTL_DEV) { sysctl_name[0] = CTL_DEV ; }
		for (mag = the_feature->scaling; mag > 0; mag --)
			value *= 10.0;
		for (; mag < 0; mag ++)
			value /= 10.0;
		* ((long *) (buf + the_feature->offset)) = (long) value;
		buflen = the_feature->offset + sizeof(long);
		if (sysctl(sysctl_name, 4, NULL, 0, buf, buflen))
			return -SENSORS_ERR_PROC;
	}
	return 0;
}

#define CURRMAG 3
#define FANMAG 0
#define INMAG 3
#define TEMPMAG 3

/*
	Returns the sysfs name and magnitude for a given feature.
	First looks for a sysfs name and magnitude in the feature structure.
	These should be added in chips.c for all non-standard feature names.
        If that fails, converts common /proc feature names
	to their sysfs equivalent, and uses common sysfs magnitude.
	If that fails, returns old /proc feature name and magnitude.

	References: doc/developers/proc in the lm_sensors package;
	            Documentation/i2c/sysfs_interface in the kernel
*/
int getsysname(const sensors_chip_feature *feature, char *sysname, int *sysmag)
{
	const char * name = feature->name;
	char last;
	char check; /* used to verify end of string */
	int num;
	
	struct match {
		const char * name, * sysname;
		const int sysmag;
	};

	struct match *m;

	struct match matches[] = {
		{ "beeps", "beep_mask", 0 },
		{ "pwm", "pwm1", 0 },
		{ "rempte_temp", "temp_input2", TEMPMAG },
		{ "remote_temp_hyst", "temp_hyst2", TEMPMAG },
		{ "remote_temp_low", "temp_min2", TEMPMAG },
		{ "remote_temp_over", "temp_max2", TEMPMAG },
		{ "temp", "temp_input1", TEMPMAG },
		{ "temp_hyst", "temp_min1", TEMPMAG },		/* kernel patch pending for hyst */
		{ "temp_low", "temp_min1", TEMPMAG },
		{ "temp_over", "temp_max1", TEMPMAG },
		{ NULL, NULL }
	};


/* use override in feature structure if present */
	if(feature->sysname != NULL) {
		strcpy(sysname, feature->sysname);
		if(feature->sysscaling)
			*sysmag = feature->sysscaling;
		else
			*sysmag = feature->scaling;
		return 0;
	}

/* check for constant mappings */
	for(m = matches; m->name != NULL; m++) {
		if(!strcmp(m->name, name)) {
			strcpy(sysname, m->sysname);
			*sysmag = m->sysmag;
			return 0;
		}
	}

/* convert common /proc names to common sysfs names */
	if(sscanf(name, "fan%d_di%c%c", &num, &last, &check) == 2 && last == 'v') {
		sprintf(sysname, "fan_div%d", num);
		*sysmag = FANMAG;
		return 0;
	}
	if(sscanf(name, "fan%d_mi%c%c", &num, &last, &check) == 2 && last == 'n') {
		sprintf(sysname, "fan_min%d", num);
		*sysmag = FANMAG;
		return 0;
	}
	if(sscanf(name, "fan%d%c", &num, &check) == 1) {
		sprintf(sysname, "fan_input%d", num);
		*sysmag = FANMAG;
		return 0;
	}

	if(sscanf(name, "in%d_mi%c%c", &num, &last, &check) == 2 && last == 'n') {
		sprintf(sysname, "in_min%d", num);
		*sysmag = INMAG;
		return 0;
	}
	if(sscanf(name, "in%d_ma%c%c", &num, &last, &check) == 2 && last == 'x') {
		sprintf(sysname, "in_max%d", num);
		*sysmag = INMAG;
		return 0;
	}
	if(sscanf(name, "in%d%c", &num, &check) == 1) {
		sprintf(sysname, "in_input%d", num);
		*sysmag = INMAG;
		return 0;
	}

	if(sscanf(name, "pwm%d%c", &num, &check) == 1) {
		strcpy(sysname, name);
		*sysmag = 0;
		return 0;
	}

	if(sscanf(name, "sensor%d%c", &num, &check) == 1) {
		strcpy(sysname, name);
		*sysmag = 0;
		return 0;
	}

	if(sscanf(name, "temp%d_hys%c%c", &num, &last, &check) == 2 && last == 't') {
		sprintf(sysname, "temp_min%d", num);	/* kernel patch pending for hyst */
		*sysmag = TEMPMAG;
		return 0;
	}
	if(sscanf(name, "temp%d_ove%c%c", &num, &last, &check) == 2 && last == 'r') {
		sprintf(sysname, "temp_max%d", num);
		*sysmag = TEMPMAG;
		return 0;
	}
	if(sscanf(name, "temp%d_mi%c%c", &num, &last, &check) == 2 && last == 'n') {
		sprintf(sysname, "temp_min%d", num);
		*sysmag = TEMPMAG;
		return 0;
	}
	if(sscanf(name, "temp%d_ma%c%c", &num, &last, &check) == 2 && last == 'x') {
		sprintf(sysname, "temp_max%d", num);
		*sysmag = TEMPMAG;
		return 0;
	}
	if(sscanf(name, "temp%d%c", &num, &check) == 1) {
		sprintf(sysname, "temp_input%d", num);
		*sysmag = TEMPMAG;
		return 0;
	}

/* bmcsensors only, not yet in kernel */
/*
	if(sscanf(name, "curr%d_mi%c%c", &num, &last, &check) == 2 && last == 'n') {
		sprintf(sysname, "curr_min%d", num);
		*sysmag = CURRMAG;
		return 0;
	}
	if(sscanf(name, "curr%d_ma%c%c", &num, &last, &check) == 2 && last == 'x') {
		sprintf(sysname, "curr_max%d", num);
		*sysmag = CURRMAG;
		return 0;
	}
	if(sscanf(name, "curr%d%c", &num, &check) == 1) {
		sprintf(sysname, "curr_input%d", num);
		*sysmag = CURRMAG;
		return 0;
	}
*/

/* give up, use old name (probably won't work though...) */
/* known to be the same:
	"alarms", "beep_enable", "vid", "vrm"
*/
	strcpy(sysname, name);
	*sysmag = feature->scaling;
	return 0;
}
