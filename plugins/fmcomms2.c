/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <glib/gthread.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <malloc.h>
#include <values.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/utsname.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../libini2.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "./block_diagram.h"
#include "dac_data_manager.h"

#define HANNING_ENBW 1.50

#define THIS_DRIVER "FMComms2/3/4"
#define PHY_DEVICE "ad9361-phy"
#define DDS_DEVICE "cf-ad9361-dds-core-lpc"
#define CAP_DEVICE "cf-ad9361-lpc"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

#define SYNC_RELOAD "SYNC_RELOAD"

extern gfloat plugin_fft_corr;
extern bool dma_valid_selection(const char *device, unsigned mask, unsigned channel_count);

static struct dac_data_manager *dac_tx_manager;

static bool is_2rx_2tx;

static const gdouble mhz_scale = 1000000.0;
static const gdouble abs_mhz_scale = -1000000.0;
static const gdouble khz_scale = 1000.0;
static const gdouble inv_scale = -1.0;

static struct iio_widget glb_widgets[50];
static struct iio_widget tx_widgets[50];
static struct iio_widget rx_widgets[50];
static unsigned int dcxo_coarse_num, dcxo_fine_num;
static unsigned int rx1_gain, rx2_gain;
static unsigned int num_glb, num_tx, num_rx;
static unsigned int rx_lo, tx_lo;
static unsigned int rx_sample_freq, tx_sample_freq;
static char last_fir_filter[PATH_MAX];
static char *rx_fastlock_store_name, *rx_fastlock_recall_name;
static char *tx_fastlock_store_name, *tx_fastlock_recall_name;

static struct iio_context *ctx;
static struct iio_device *dev, *dds, *cap;

#define SECTION_GLOBAL 0
#define SECTION_TX 1
#define SECTION_RX 2
#define SECTION_FPGA 3
static GtkToggleToolButton *section_toggle[4];
static GtkWidget *section_setting[4];

/* Widgets for Global Settings */
static GtkWidget *ensm_mode;
static GtkWidget *ensm_mode_available;
static GtkWidget *calib_mode;
static GtkWidget *calib_mode_available;
static GtkWidget *trx_rate_governor;
static GtkWidget *trx_rate_governor_available;
static GtkWidget *filter_fir_config;

/* Widgets for Receive Settings */
static GtkWidget *rx_gain_control_rx1;
static GtkWidget *rx_gain_control_modes_rx1;
static GtkWidget *rf_port_select_rx;
static GtkWidget *rx_gain_control_rx2;
static GtkWidget *rx_gain_control_modes_rx2;
static GtkWidget *rx1_rssi;
static GtkWidget *rx2_rssi;
static GtkWidget *rx_path_rates;
static GtkWidget *tx_path_rates;
static GtkWidget *fir_filter_en_tx;
static GtkWidget *enable_fir_filter_rx;
static GtkWidget *enable_fir_filter_rx_tx;
static GtkWidget *disable_all_fir_filters;
static GtkWidget *rf_port_select_tx;
static GtkWidget *rx_fastlock_profile;
static GtkWidget *tx_fastlock_profile;
static GtkWidget *rx_phase_rotation[2];

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *fmcomms2_panel;
static gboolean plugin_detached;

static const char *fmcomms2_sr_attribs[] = {
	PHY_DEVICE".trx_rate_governor",
	PHY_DEVICE".dcxo_tune_coarse",
	PHY_DEVICE".dcxo_tune_fine",
	PHY_DEVICE".ensm_mode",
	PHY_DEVICE".in_voltage0_rf_port_select",
	PHY_DEVICE".in_voltage0_gain_control_mode",
	PHY_DEVICE".in_voltage0_hardwaregain",
	PHY_DEVICE".in_voltage1_gain_control_mode",
	PHY_DEVICE".in_voltage1_hardwaregain",
	PHY_DEVICE".in_voltage_bb_dc_offset_tracking_en",
	PHY_DEVICE".in_voltage_quadrature_tracking_en",
	PHY_DEVICE".in_voltage_rf_dc_offset_tracking_en",
	PHY_DEVICE".out_voltage0_rf_port_select",
	PHY_DEVICE".out_altvoltage0_RX_LO_frequency",
	PHY_DEVICE".out_altvoltage1_TX_LO_frequency",
	PHY_DEVICE".out_voltage0_hardwaregain",
	PHY_DEVICE".out_voltage1_hardwaregain",
	PHY_DEVICE".out_voltage_sampling_frequency",
	PHY_DEVICE".in_voltage_rf_bandwidth",
	PHY_DEVICE".out_voltage_rf_bandwidth",
	PHY_DEVICE".in_voltage_filter_fir_en",
	PHY_DEVICE".out_voltage_filter_fir_en",
	PHY_DEVICE".in_out_voltage_filter_fir_en",
	DDS_DEVICE".out_altvoltage0_TX1_I_F1_frequency",
	DDS_DEVICE".out_altvoltage0_TX1_I_F1_phase",
	DDS_DEVICE".out_altvoltage0_TX1_I_F1_raw",
	DDS_DEVICE".out_altvoltage0_TX1_I_F1_scale",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_frequency",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_phase",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_raw",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_scale",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_frequency",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_phase",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_raw",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_scale",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_frequency",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_phase",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_raw",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_scale",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_frequency",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_phase",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_raw",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_scale",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_frequency",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_phase",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_raw",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_scale",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_frequency",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_phase",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_raw",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_scale",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_frequency",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_phase",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_raw",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_scale",
};

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static void glb_settings_update_labels(void)
{
	float rates[6];
	char tmp[160], buf[1024];
	ssize_t ret;

	ret = iio_device_attr_read(dev, "ensm_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(ensm_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(ensm_mode), "<error>");

	ret = iio_device_attr_read(dev, "calib_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(calib_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(calib_mode), "<error>");

	ret = iio_device_attr_read(dev, "trx_rate_governor", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(trx_rate_governor), buf);
	else
		gtk_label_set_text(GTK_LABEL(trx_rate_governor), "<error>");

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev, "voltage0", false),
			"gain_control_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(rx_gain_control_rx1), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx_gain_control_rx1), "<error>");

	if (is_2rx_2tx) {
		ret = iio_channel_attr_read(
				iio_device_find_channel(dev, "voltage1", false),
				"gain_control_mode", buf, sizeof(buf));
		if (ret > 0)
			gtk_label_set_text(GTK_LABEL(rx_gain_control_rx2), buf);
		else
			gtk_label_set_text(GTK_LABEL(rx_gain_control_rx2), "<error>");
	}

	ret = iio_device_attr_read(dev, "rx_path_rates", buf, sizeof(buf));
	if (ret > 0) {
		sscanf(buf, "BBPLL:%f ADC:%f R2:%f R1:%f RF:%f RXSAMP:%f",
		        &rates[0], &rates[1], &rates[2], &rates[3], &rates[4],
			&rates[5]);
		sprintf(tmp, "BBPLL: %4.3f   ADC: %4.3f   R2: %4.3f   R1: %4.3f   RF: %4.3f   RXSAMP: %4.3f",
		        rates[0] / 1e6, rates[1] / 1e6, rates[2] / 1e6,
			rates[3] / 1e6, rates[4] / 1e6, rates[5] / 1e6);

		gtk_label_set_text(GTK_LABEL(rx_path_rates), tmp);
	} else {
		gtk_label_set_text(GTK_LABEL(rx_path_rates), "<error>");
	}

	ret = iio_device_attr_read(dev, "tx_path_rates", buf, sizeof(buf));
	if (ret > 0) {
		sscanf(buf, "BBPLL:%f DAC:%f T2:%f T1:%f TF:%f TXSAMP:%f",
		        &rates[0], &rates[1], &rates[2], &rates[3], &rates[4],
			&rates[5]);
		sprintf(tmp, "BBPLL: %4.3f   DAC: %4.3f   T2: %4.3f   T1: %4.3f   TF: %4.3f   TXSAMP: %4.3f",
		        rates[0] / 1e6, rates[1] / 1e6, rates[2] / 1e6,
			rates[3] / 1e6, rates[4] / 1e6, rates[5] / 1e6);

		gtk_label_set_text(GTK_LABEL(tx_path_rates), tmp);
	} else {
		gtk_label_set_text(GTK_LABEL(tx_path_rates), "<error>");
	}

	iio_widget_update(&rx_widgets[rx1_gain]);
	if (is_2rx_2tx)
		iio_widget_update(&rx_widgets[rx2_gain]);
}

static void sample_frequency_changed_cb(void *data)
{
	glb_settings_update_labels();
	rx_update_labels();
}

static void rssi_update_labels(void)
{
	char buf[1024];
	int ret;

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev, "voltage0", false),
			"rssi", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(rx1_rssi), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx1_rssi), "<error>");

	if (!is_2rx_2tx)
		return;

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev, "voltage1", false),
			"rssi", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(rx2_rssi), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx2_rssi), "<error>");
}

static void update_display (void *ptr)
{
	const char *gain_mode;

	/* This thread never exists, and just updates the control frame */
	while (1) {
		if (this_page == gtk_notebook_get_current_page(nbook) || plugin_detached) {
			gdk_threads_enter();
			rssi_update_labels();
			gain_mode = gtk_combo_box_get_active_text(GTK_COMBO_BOX(rx_gain_control_modes_rx1));
			if (gain_mode && strcmp(gain_mode, "manual"))
				iio_widget_update(&rx_widgets[rx1_gain]);

			gain_mode = gtk_combo_box_get_active_text(GTK_COMBO_BOX(rx_gain_control_modes_rx2));
			if (is_2rx_2tx && gain_mode && strcmp(gain_mode, "manual"))
				iio_widget_update(&rx_widgets[rx2_gain]);
			gdk_threads_leave();
		}
		sleep(1);
	}
}

void filter_fir_update(void)
{
	bool rx = false, tx = false, rxtx = false;
	struct iio_channel *chn;

	iio_device_attr_read_bool(dev, "in_out_voltage_filter_fir_en", &rxtx);

	chn = iio_device_find_channel(dev, "voltage0", false);
	if (chn)
		iio_channel_attr_read_bool(chn, "filter_fir_en", &rx);
	chn = iio_device_find_channel(dev, "voltage0", true);
	if (chn)
		iio_channel_attr_read_bool(chn, "filter_fir_en", &tx);

	if (rxtx) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx_tx), rxtx);
	} else if (!rx && !tx) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (disable_all_fir_filters), true);
	} else {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx), rx);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (fir_filter_en_tx), tx);
	}
}

void filter_fir_enable(GtkToggleButton *button, gpointer data)
{
	bool rx, tx, rxtx, disable;

	if (gtk_toggle_button_get_active(button))
		return;

	rx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx));
	tx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (fir_filter_en_tx));
	rxtx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx_tx));
	disable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (disable_all_fir_filters));

	if (rxtx || disable) {
		iio_device_attr_write_bool(dev,
				"in_out_voltage_filter_fir_en", rxtx);
	} else {
		struct iio_channel *chn;
		chn = iio_device_find_channel(dev, "voltage0", true);
		if (chn)
			iio_channel_attr_write_bool(chn, "filter_fir_en", tx);

		chn = iio_device_find_channel(dev, "voltage0", false);
		if (chn)
			iio_channel_attr_write_bool(chn, "filter_fir_en", rx);
	}

	filter_fir_update();
	glb_settings_update_labels();
}

static void rx_phase_rotation_update()
{
	struct iio_channel *out[4];
	gdouble val[4];
	int i, d = 0;

	out[0] = iio_device_find_channel(cap, "voltage0", false);
	out[1] = iio_device_find_channel(cap, "voltage1", false);

	if (is_2rx_2tx) {
		out[2] = iio_device_find_channel(cap, "voltage2", false);
		out[3] = iio_device_find_channel(cap, "voltage3", false);
		d = 2;
	}

	for (i = 0; i <= d; i += 2) {
		iio_channel_attr_read_double(out[i], "calibscale", &val[0]);
		iio_channel_attr_read_double(out[i], "calibphase", &val[1]);
		iio_channel_attr_read_double(out[i + 1], "calibscale", &val[2]);
		iio_channel_attr_read_double(out[i + 1], "calibphase", &val[3]);

		val[0] = acos(val[0]) * 360.0 / (2.0 * M_PI);
		val[1] = asin(-1.0 * val[1]) * 360.0 / (2.0 * M_PI);
		val[2] = acos(val[2]) * 360.0 / (2.0 * M_PI);
		val[3] = asin(val[3]) * 360.0 / (2.0 * M_PI);

		if (val[1] < 0.0)
			val[0] *= -1.0;
		if (val[3] < 0.0)
			val[2] *= -1.0;
		if (val[1] < -90.0)
			val[0] = (val[0] * -1.0) - 180.0;
		if (val[3] < -90.0)
			val[0] = (val[0] * -1.0) - 180.0;

		if (fabs(val[0]) > 90.0) {
			if (val[1] < 0.0)
				val[1] = (val[1] * -1.0) - 180.0;
			else
				val[1] = 180 - val[1];
		}
		if (fabs(val[2]) > 90.0) {
			if (val[3] < 0.0)
				val[3] = (val[3] * -1.0) - 180.0;
			else
				val[3] = 180 - val[3];
		}

		if (round(val[0]) != round(val[1]) &&
					round(val[0]) != round(val[2]) &&
					round(val[0]) != round(val[3])) {
			printf("error calculating phase rotations\n");
			val[0] = 0.0;
		} else
			val[0] = (val[0] + val[1] + val[2] + val[3]) / 4.0;

		gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_phase_rotation[i/2]), val[0]);
	}
}

static void dxco_widgets_update(void)
{
	char val[64];
	int ret;

	ret = iio_device_attr_read(dev, "dcxo_tune_coarse", val, sizeof(val));
	if (ret > 0)
		gtk_widget_show(glb_widgets[dcxo_coarse_num].widget);
	ret = iio_device_attr_read(dev, "dcxo_tune_fine", val, sizeof(val));
	if (ret > 0)
		gtk_widget_show(glb_widgets[dcxo_fine_num].widget);
}

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	iio_update_widgets(glb_widgets, num_glb);
	iio_update_widgets(tx_widgets, num_tx);
	iio_update_widgets(rx_widgets, num_rx);
	dac_data_manager_update_iio_widgets(dac_tx_manager);
	filter_fir_update();
	rx_update_labels();
	glb_settings_update_labels();
	rssi_update_labels();
	rx_phase_rotation_update();
	dxco_widgets_update();
}

static void hide_section_cb(GtkToggleToolButton *btn, GtkWidget *section)
{
	GtkWidget *toplevel;

	if (gtk_toggle_tool_button_get_active(btn)) {
		gtk_object_set(GTK_OBJECT(btn), "stock-id", "gtk-go-down", NULL);
		gtk_widget_show(section);
	} else {
		gtk_object_set(GTK_OBJECT(btn), "stock-id", "gtk-go-up", NULL);
		gtk_widget_hide(section);
		toplevel = gtk_widget_get_toplevel(GTK_WIDGET(btn));
		if (GTK_WIDGET_TOPLEVEL(toplevel))
			gtk_window_resize (GTK_WINDOW(toplevel), 1, 1);
	}
}

static int write_int(struct iio_channel *chn, const char *attr, int val)
{
	return iio_channel_attr_write_longlong(chn, attr, (long long) val);
}

static void fastlock_clicked(GtkButton *btn, gpointer data)
{
	int profile;

	switch ((uintptr_t) data) {
		case 1: /* RX Store */
			iio_widget_save(&rx_widgets[rx_lo]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage0", true),
					rx_fastlock_store_name, profile);
			break;
		case 2: /* TX Store */
			iio_widget_save(&tx_widgets[tx_lo]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage1", true),
					tx_fastlock_store_name, profile);
			break;
		case 3: /* RX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage0", true),
					rx_fastlock_recall_name, profile);
			iio_widget_update(&rx_widgets[rx_lo]);
			break;
		case 4: /* TX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage1", true),
					tx_fastlock_recall_name, profile);
			iio_widget_update(&tx_widgets[tx_lo]);
			break;
	}
}

static int load_fir_filter(const char *file_name)
{
	int ret = -1;
	FILE *f = fopen(file_name, "r");
	if (f) {
		char *buf;
		ssize_t len;

		fseek(f, 0, SEEK_END);
		len = ftell(f);
		buf = malloc(len);
		fseek(f, 0, SEEK_SET);
		len = fread(buf, 1, len, f);
		fclose(f);

		ret = iio_device_attr_write_raw(dev,
				"filter_fir_config", buf, len);
		free(buf);
	}

	if (ret < 0) {
		fprintf(stderr, "FIR filter config failed\n");
		GtkWidget *toplevel = gtk_widget_get_toplevel(fmcomms2_panel);
		if (!gtk_widget_is_toplevel(toplevel))
			toplevel = NULL;

		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(toplevel),
						GTK_DIALOG_MODAL,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_CLOSE,
						"\nFailed to configure the FIR filter using the selected file.");
		gtk_window_set_title(GTK_WINDOW(dialog), "FIR Filter Configuration Failed");
		if (gtk_dialog_run(GTK_DIALOG(dialog)))
			gtk_widget_destroy(dialog);

	} else {
		strcpy(last_fir_filter, file_name);
	}

	return ret;
}

void filter_fir_config_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	char *file_name = gtk_file_chooser_get_filename(chooser);

	if (load_fir_filter(file_name) < 0) {
		if (strlen(last_fir_filter) == 0)
			gtk_file_chooser_set_filename(chooser, "(None)");
		else
			gtk_file_chooser_set_filename(chooser, last_fir_filter);
	}
}

static int compare_gain(const char *a, const char *b) __attribute__((unused));
static int compare_gain(const char *a, const char *b)
{
	double val_a, val_b;
	sscanf(a, "%lf", &val_a);
	sscanf(b, "%lf", &val_b);

	if (val_a < val_b)
		return -1;
	else if(val_a > val_b)
		return 1;
	else
		return 0;
}

static void tx_sample_rate_changed(GtkSpinButton *spinbutton, gpointer user_data)
{
	gdouble rate;

	rate = gtk_spin_button_get_value(spinbutton) / 2.0;
	dac_data_manager_freq_widgets_range_update(dac_tx_manager, rate);
}

static void rx_phase_rotation_set(GtkSpinButton *spinbutton, gpointer user_data)
{
	glong offset = (glong) user_data;
	struct iio_channel *out0, *out1;
	gdouble val, phase;

	val = gtk_spin_button_get_value(spinbutton);

	phase = val * 2 * M_PI / 360.0;

	if (offset == 2) {
		out0 = iio_device_find_channel(cap, "voltage2", false);
		out1 = iio_device_find_channel(cap, "voltage3", false);
	} else {
		out0 = iio_device_find_channel(cap, "voltage0", false);
		out1 = iio_device_find_channel(cap, "voltage1", false);
	}

	if (out1 && out0) {
		iio_channel_attr_write_double(out0, "calibscale", (double) cos(phase));
		iio_channel_attr_write_double(out0, "calibphase", (double) (-1 * sin(phase)));
		iio_channel_attr_write_double(out1, "calibscale", (double) cos(phase));
		iio_channel_attr_write_double(out1, "calibphase", (double) sin(phase));
	}
}

/* Check for a valid two channels combination (ch0->ch1, ch2->ch3, ...)
 *
 * struct iio_channel_info *chanels - list of channels of a device
 * int ch_count - number of channel in the list
 * char* ch_name - output parameter: stores references to the enabled
 *                 channels.
 * Return 1 if the channel combination is valid
 * Return 0 if the combination is not valid
 */
int channel_combination_check(struct iio_device *dev, const char **ch_names)
{
	bool consecutive_ch = FALSE;
	unsigned int i, k, nb_channels = iio_device_get_channels_count(dev);

	for (i = 0, k = 0; i < nb_channels; i++) {
		struct iio_channel *ch = iio_device_get_channel(dev, i);
		struct extra_info *info = iio_channel_get_data(ch);

		if (info->may_be_enabled) {
			const char *name = iio_channel_get_name(ch) ?: iio_channel_get_id(ch);
			ch_names[k++] = name;

			if (i > 0) {
				struct extra_info *prev = iio_channel_get_data(iio_device_get_channel(dev, i - 1));
				if (prev->may_be_enabled) {
					consecutive_ch = TRUE;
					break;
				}
			}
		}
	}
	if (!consecutive_ch)
		return 0;

	if (!(i & 0x1))
		return 0;

	return 1;
}

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
}

static void make_widget_update_signal_based(struct iio_widget *widgets,
	unsigned int num_widgets)
{
	char signal_name[25];
	unsigned int i;

	for (i = 0; i < num_widgets; i++) {
		if (GTK_IS_CHECK_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_TOGGLE_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_SPIN_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "value-changed");
		else if (GTK_IS_COMBO_BOX_TEXT(widgets[i].widget))
			sprintf(signal_name, "%s", "changed");
		else
			printf("unhandled widget type, attribute: %s\n", widgets[i].attr_name);

		if (GTK_IS_SPIN_BUTTON(widgets[i].widget) &&
			widgets[i].priv_progress != NULL) {
				iio_spin_button_progress_activate(&widgets[i]);
		} else {
			g_signal_connect(G_OBJECT(widgets[i].widget), signal_name, G_CALLBACK(save_widget_value), &widgets[i]);
		}
	}
}

int handle_external_request (const char *request)
{
	int ret = 0;

	if (!strcmp(request, "Reload Settings")) {
		reload_button_clicked(NULL, 0);
		ret = 1;
	}

	return ret;
}

static void load_profile(const char *ini_fn)
{
	char *value;

	update_from_ini(ini_fn, THIS_DRIVER, dev, fmcomms2_sr_attribs,
			ARRAY_SIZE(fmcomms2_sr_attribs));
	update_from_ini(ini_fn, THIS_DRIVER, dds, fmcomms2_sr_attribs,
			ARRAY_SIZE(fmcomms2_sr_attribs));

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "load_fir_filter_file");
	if (value) {
		if (value[0]) {
			load_fir_filter(value);
			gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(filter_fir_config), value);
		}
		free(value);
	}

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "dds_mode_tx1");
	if (value) {
		dac_data_manager_set_dds_mode(dac_tx_manager, DDS_DEVICE, 1, atoi(value));
		free(value);
	}

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "dds_mode_tx2");
	if (value) {
		dac_data_manager_set_dds_mode(dac_tx_manager, DDS_DEVICE, 2, atoi(value));
		free(value);
	}

	if (dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE, 1) == DDS_BUFFER) {
		value = read_token_from_ini(ini_fn, THIS_DRIVER, "dac_buf_filename");
		if (value) {
			dac_data_manager_set_buffer_chooser_filename(dac_tx_manager, value);
			free(value);
		}
	}

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "global_settings_show");
	if (value) {
		gtk_toggle_tool_button_set_active(section_toggle[SECTION_GLOBAL], !!atoi(value));
		hide_section_cb(section_toggle[SECTION_GLOBAL], section_setting[SECTION_GLOBAL]);
		free(value);
	}

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "tx_show");
	if (value) {
		gtk_toggle_tool_button_set_active(section_toggle[SECTION_TX], !!atoi(value));
		hide_section_cb(section_toggle[SECTION_TX], section_setting[SECTION_TX]);
		free(value);
	}

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "rx_show");
	if (value) {
		gtk_toggle_tool_button_set_active(section_toggle[SECTION_RX], !!atoi(value));
		hide_section_cb(section_toggle[SECTION_RX], section_setting[SECTION_RX]);
		free(value);
	}

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "fpga_show");
	if (value) {
		gtk_toggle_tool_button_set_active(section_toggle[SECTION_FPGA], !!atoi(value));
		hide_section_cb(section_toggle[SECTION_FPGA], section_setting[SECTION_FPGA]);
		free(value);
	}

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "tx_channel_0");
	if (value) {
		dac_data_manager_set_tx_channel_state(dac_tx_manager, 0, !!atoi(value));
		free(value);
	}

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "tx_channel_1");
	if (value) {
		dac_data_manager_set_tx_channel_state(dac_tx_manager, 1, !!atoi(value));
		free(value);
	}

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "tx_channel_2");
	if (value) {
		dac_data_manager_set_tx_channel_state(dac_tx_manager, 2, !!atoi(value));
		free(value);
	}

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "tx_channel_3");
	if (value) {
		dac_data_manager_set_tx_channel_state(dac_tx_manager, 3, !!atoi(value));
		free(value);
	}
}

static GtkWidget * fmcomms2_init(GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *dds_container;
	struct iio_channel *ch0, *ch1;
	const char *freq_name;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dev = iio_context_find_device(ctx, PHY_DEVICE);
	dds = iio_context_find_device(ctx, DDS_DEVICE);
	cap = iio_context_find_device(ctx, CAP_DEVICE);
	ch0 = iio_device_find_channel(dev, "voltage0", false);
	ch1 = iio_device_find_channel(dev, "voltage1", false);

	dac_tx_manager = dac_data_manager_new(dds, NULL, ctx);
	if (!dac_tx_manager) {
		iio_context_destroy(ctx);
		return NULL;
	}

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (!gtk_builder_add_from_file(builder, "fmcomms2.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "fmcomms2.glade", NULL);

	is_2rx_2tx = ch1 && iio_channel_find_attr(ch1, "hardwaregain");

	fmcomms2_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms2_panel"));

	/* Global settings */

	ensm_mode = GTK_WIDGET(gtk_builder_get_object(builder, "ensm_mode"));
	ensm_mode_available = GTK_WIDGET(gtk_builder_get_object(builder, "ensm_mode_available"));
	calib_mode = GTK_WIDGET(gtk_builder_get_object(builder, "calib_mode"));
	calib_mode_available = GTK_WIDGET(gtk_builder_get_object(builder, "calib_mode_available"));
	trx_rate_governor = GTK_WIDGET(gtk_builder_get_object(builder, "trx_rate_governor"));
	trx_rate_governor_available = GTK_WIDGET(gtk_builder_get_object(builder, "trx_rate_governor_available"));
	tx_path_rates = GTK_WIDGET(gtk_builder_get_object(builder, "label_tx_path"));
	rx_path_rates = GTK_WIDGET(gtk_builder_get_object(builder, "label_rx_path"));
	filter_fir_config = GTK_WIDGET(gtk_builder_get_object(builder, "filter_fir_config"));
	enable_fir_filter_rx = GTK_WIDGET(gtk_builder_get_object(builder, "enable_fir_filter_rx"));
	fir_filter_en_tx = GTK_WIDGET(gtk_builder_get_object(builder, "fir_filter_en_tx"));
	enable_fir_filter_rx_tx = GTK_WIDGET(gtk_builder_get_object(builder, "enable_fir_filter_tx_rx"));
	disable_all_fir_filters = GTK_WIDGET(gtk_builder_get_object(builder, "disable_all_fir_filters"));

	section_toggle[SECTION_GLOBAL] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "global_settings_toggle"));
	section_setting[SECTION_GLOBAL] = GTK_WIDGET(gtk_builder_get_object(builder, "global_settings"));
	section_toggle[SECTION_TX] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "tx_toggle"));
	section_setting[SECTION_TX] = GTK_WIDGET(gtk_builder_get_object(builder, "tx_settings"));
	section_toggle[SECTION_RX] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "rx_toggle"));
	section_setting[SECTION_RX] = GTK_WIDGET(gtk_builder_get_object(builder, "rx_settings"));
	section_toggle[SECTION_FPGA] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "fpga_toggle"));
	section_setting[SECTION_FPGA] = GTK_WIDGET(gtk_builder_get_object(builder, "fpga_settings"));

	/* Receive Chain */

	rf_port_select_rx = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_rx"));
	rx_gain_control_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx1"));
	rx_gain_control_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx2"));
	rx_gain_control_modes_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx1"));
	rx_gain_control_modes_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx2"));
	rx1_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx1"));
	rx2_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx2"));
	rx_fastlock_profile = GTK_WIDGET(gtk_builder_get_object(builder, "rx_fastlock_profile"));

	/* Transmit Chain */

	rf_port_select_tx = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_tx"));
	tx_fastlock_profile = GTK_WIDGET(gtk_builder_get_object(builder, "tx_fastlock_profile"));
	dds_container = GTK_WIDGET(gtk_builder_get_object(builder, "dds_transmit_block"));
	gtk_container_add(GTK_CONTAINER(dds_container), dac_data_manager_get_gui_container(dac_tx_manager));
	gtk_widget_show_all(dds_container);

	rx_phase_rotation[0] = GTK_WIDGET(gtk_builder_get_object(builder, "rx1_phase_rotation"));
	rx_phase_rotation[1] = GTK_WIDGET(gtk_builder_get_object(builder, "rx2_phase_rotation"));

	gtk_combo_box_set_active(GTK_COMBO_BOX(ensm_mode_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(trx_rate_governor_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes_rx1), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes_rx2), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rf_port_select_rx), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rf_port_select_tx), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_fastlock_profile), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(tx_fastlock_profile), 0);

	/* Set FMCOMMS2/3 max sampling freq -> 61.44MHz and FMCOMMS4 -> 122.88 */
	GtkWidget *sfreq = GTK_WIDGET(gtk_builder_get_object(builder, "sampling_freq_tx"));
	GtkAdjustment *sfreq_adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(sfreq));

	if (is_2rx_2tx)
		gtk_adjustment_set_upper(sfreq_adj, 61.44);
	else
		gtk_adjustment_set_upper(sfreq_adj, 122.88);

	/* Bind the IIO device files to the GUI widgets */

	/* Global settings */
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev, NULL, "ensm_mode", "ensm_mode_available",
		ensm_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev, NULL, "calib_mode", "calib_mode_available",
		calib_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev, NULL, "trx_rate_governor", "trx_rate_governor_available",
		trx_rate_governor_available, NULL);

	dcxo_coarse_num = num_glb;
	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev, NULL, "dcxo_tune_coarse", builder, "dcxo_coarse_tune",
		0);
	dcxo_fine_num = num_glb;
	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev, NULL, "dcxo_tune_fine", builder, "dcxo_fine_tune",
		0);

	/* Receive Chain */

	iio_combo_box_init(&rx_widgets[num_rx++],
		dev, ch0, "gain_control_mode",
		"gain_control_mode_available",
		rx_gain_control_modes_rx1, NULL);

	iio_combo_box_init(&rx_widgets[num_rx++],
		dev, ch0, "rf_port_select",
		"rf_port_select_available",
		rf_port_select_rx, NULL);

	if (is_2rx_2tx)
		iio_combo_box_init(&rx_widgets[num_rx++],
			dev, ch1, "gain_control_mode",
			"gain_control_mode_available",
			rx_gain_control_modes_rx2, NULL);
	rx1_gain = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "hardwaregain", builder,
		"hardware_gain_rx1", NULL);

	if (is_2rx_2tx) {
		rx2_gain = num_rx;
		iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			dev, ch1, "hardwaregain", builder,
			"hardware_gain_rx2", NULL);
	}
	rx_sample_freq = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "sampling_frequency", builder,
		"sampling_freq_rx", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "rf_bandwidth", builder, "rf_bandwidth_rx",
		&mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);
	rx_lo = num_rx;

	ch1 = iio_device_find_channel(dev, "altvoltage0", true);
	if (iio_channel_find_attr(ch1, "frequency"))
		freq_name = "frequency";
	else
		freq_name = "RX_LO_frequency";
	iio_spin_button_s64_init_from_builder(&rx_widgets[num_rx++],
		dev, ch1, freq_name, builder,
		"rx_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "quadrature_tracking_en", builder,
		"quad", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "rf_dc_offset_tracking_en", builder,
		"rfdc", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "bb_dc_offset_tracking_en", builder,
		"bbdc", 0);

	iio_spin_button_init_from_builder(&rx_widgets[num_rx],
		dev, ch1, "calibphase",
		builder, "rx1_phase_rotation", NULL);
	iio_spin_button_add_progress(&rx_widgets[num_rx++]);

	ch0 = iio_device_find_channel(dev, "altvoltage0", true);

	if (iio_channel_find_attr(ch0, "fastlock_store"))
		rx_fastlock_store_name = "fastlock_store";
	else
		rx_fastlock_store_name = "RX_LO_fastlock_store";
	if (iio_channel_find_attr(ch0, "fastlock_recall"))
		rx_fastlock_recall_name = "fastlock_recall";
	else
		rx_fastlock_recall_name = "RX_LO_fastlock_recall";

	/* Transmit Chain */

	ch0 = iio_device_find_channel(dev, "voltage0", true);
	if (is_2rx_2tx)
		ch1 = iio_device_find_channel(dev, "voltage1", true);

	iio_combo_box_init(&tx_widgets[num_tx++],
		dev, ch0, "rf_port_select",
		"rf_port_select_available",
		rf_port_select_tx, NULL);

	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
		dev, ch0, "hardwaregain", builder,
		"hardware_gain_tx1", &inv_scale);

	if (is_2rx_2tx)
		iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, ch1, "hardwaregain", builder,
			"hardware_gain_tx2", &inv_scale);
	tx_sample_freq = num_tx;
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		dev, ch0, "sampling_frequency", builder,
		"sampling_freq_tx", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		dev, ch0, "rf_bandwidth", builder,
		"rf_bandwidth_tx", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	tx_lo = num_tx;
	ch1 = iio_device_find_channel(dev, "altvoltage1", true);

	if (iio_channel_find_attr(ch1, "frequency"))
		freq_name = "frequency";
	else
		freq_name = "TX_LO_frequency";
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
		dev, ch1, freq_name, builder, "tx_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	ch1 = iio_device_find_channel(dev, "altvoltage1", true);

	if (ini_fn)
		load_profile(ini_fn);

	/* Update all widgets with current values */
	printf("Updating GLB widgets...\n");
	iio_update_widgets(glb_widgets, num_glb);
	printf("Updating TX values...\n");
	tx_update_values();
	printf("Updating RX values...\n");
	rx_update_values();
	printf("Updating FIR filter...\n");
	filter_fir_update();
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(disable_all_fir_filters), true);
	glb_settings_update_labels();
	rssi_update_labels();
	dac_data_manager_update_iio_widgets(dac_tx_manager);

	/* Connect signals */

	if (iio_channel_find_attr(ch1, "fastlock_store"))
		tx_fastlock_store_name = "fastlock_store";
	else
		tx_fastlock_store_name = "TX_LO_fastlock_store";
	if (iio_channel_find_attr(ch1, "fastlock_recall"))
		tx_fastlock_recall_name = "fastlock_recall";
	else
		tx_fastlock_recall_name = "TX_LO_fastlock_recall";

	g_builder_connect_signal(builder, "rx1_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)0);

	g_builder_connect_signal(builder, "rx2_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)2);

	g_builder_connect_signal(builder, "sampling_freq_tx", "value-changed",
			G_CALLBACK(tx_sample_rate_changed), NULL);

	g_builder_connect_signal(builder, "fmcomms2_settings_reload", "clicked",
		G_CALLBACK(reload_button_clicked), NULL);

	g_builder_connect_signal(builder, "filter_fir_config", "file-set",
		G_CALLBACK(filter_fir_config_file_set_cb), NULL);

	g_builder_connect_signal(builder, "rx_fastlock_store", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 1);
	g_builder_connect_signal(builder, "tx_fastlock_store", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 2);
	g_builder_connect_signal(builder, "rx_fastlock_recall", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 3);
	g_builder_connect_signal(builder, "tx_fastlock_recall", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 4);

	g_builder_connect_signal(builder, "sampling_freq_tx", "value-changed",
			G_CALLBACK(tx_sample_rate_changed), NULL);

	g_signal_connect_after(section_toggle[SECTION_GLOBAL], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_GLOBAL]);

	g_signal_connect_after(section_toggle[SECTION_TX], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_TX]);

	g_signal_connect_after(section_toggle[SECTION_RX], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_RX]);

	g_signal_connect_after(section_toggle[SECTION_FPGA], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_FPGA]);

	g_signal_connect_after(ensm_mode_available, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect_after(calib_mode_available, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect_after(trx_rate_governor_available, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect_after(rx_gain_control_modes_rx1, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);
	g_signal_connect_after(rx_gain_control_modes_rx2, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect_after(enable_fir_filter_rx, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);
	g_signal_connect_after(fir_filter_en_tx, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);
	g_signal_connect_after(enable_fir_filter_rx_tx, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);
	g_signal_connect_after(disable_all_fir_filters, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);

	make_widget_update_signal_based(glb_widgets, num_glb);
	make_widget_update_signal_based(rx_widgets, num_rx);
	make_widget_update_signal_based(tx_widgets, num_tx);

	iio_spin_button_set_on_complete_function(&rx_widgets[rx_sample_freq],
		sample_frequency_changed_cb, NULL);
	iio_spin_button_set_on_complete_function(&tx_widgets[tx_sample_freq],
		sample_frequency_changed_cb, NULL);
	iio_spin_button_set_on_complete_function(&rx_widgets[rx_lo],
		sample_frequency_changed_cb, NULL);
	iio_spin_button_set_on_complete_function(&tx_widgets[tx_lo],
		sample_frequency_changed_cb, NULL);

	add_ch_setup_check_fct("cf-ad9361-lpc", channel_combination_check);
	plugin_fft_corr = 20 * log10(1/sqrt(HANNING_ENBW));

	block_diagram_init(builder, 2, "fmcomms2.svg", "AD_FMCOMM2S2_RevC.jpg");

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(filter_fir_config), OSC_FILTER_FILE_PATH);
	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);

	if (!is_2rx_2tx) {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_rx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_fpga_rx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "table_hw_gain_tx2")));
	}

	g_thread_new("Update_thread", (void *) &update_display, NULL);

	return fmcomms2_panel;
}

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void fmcomms2_get_preferred_size(int *width, int *height)
{
	if (width)
		*width = 1100;
	if (height)
		*height = 800;
}

static void save_widgets_to_ini(FILE *f)
{
	char buf[0x1000];

	snprintf(buf, sizeof(buf), "load_fir_filter_file = %s\n"
			"dds_mode_tx1 = %i\n"
			"dds_mode_tx2 = %i\n"
			"dac_buf_filename = %s\n"
			"tx_channel_0 = %i\n"
			"tx_channel_1 = %i\n"
			"tx_channel_2 = %i\n"
			"tx_channel_3 = %i\n"
			"global_settings_show = %i\n"
			"tx_show = %i\n"
			"rx_show = %i\n"
			"fpga_show = %i\n",
			last_fir_filter,
			dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE, 1),
			dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE, 2),
			dac_data_manager_get_buffer_chooser_filename(dac_tx_manager),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 0),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 1),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 2),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 3),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_GLOBAL]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_TX]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_RX]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_FPGA]));
	fwrite(buf, 1, strlen(buf), f);
}

static void save_profile(const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		save_to_ini(f, THIS_DRIVER, dev, fmcomms2_sr_attribs,
				ARRAY_SIZE(fmcomms2_sr_attribs));
		save_to_ini(f, NULL, dds, fmcomms2_sr_attribs,
				ARRAY_SIZE(fmcomms2_sr_attribs));
		save_widgets_to_ini(f);
		fclose(f);
	}
}

static void context_destroy(const char *ini_fn)
{
	save_profile(ini_fn);

	if (dac_tx_manager) {
		dac_data_manager_free(dac_tx_manager);
		dac_tx_manager = NULL;
	}

	iio_context_destroy(ctx);
}

struct osc_plugin plugin;

static bool fmcomms2_identify(void)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	if (!iio_context_find_device(osc_ctx, PHY_DEVICE)
		|| !iio_context_find_device(osc_ctx, DDS_DEVICE))
		return false;

	/* Check if FMComms5 is used */
	return !iio_context_find_device(osc_ctx, "ad9361-phy-B");
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = fmcomms2_identify,
	.init = fmcomms2_init,
	.handle_external_request = handle_external_request,
	.update_active_page = update_active_page,
	.get_preferred_size = fmcomms2_get_preferred_size,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
