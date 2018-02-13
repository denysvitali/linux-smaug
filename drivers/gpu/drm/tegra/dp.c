#include <drm/drmP.h>

#include "dp.h"

struct drm_dp_link_tegra {
	struct drm_dp_link base;

	const struct drm_dp_link_ops *ops;
	struct drm_dp_aux *aux;

	struct drm_dp_link_train train;
};

static inline struct drm_dp_link_tegra *
to_drm_dp_link_tegra(struct drm_dp_link *link)
{
	return container_of(link, struct drm_dp_link_tegra, base);
}

int __drm_dp_link_configure(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	int err;

	if (tegra->ops && tegra->ops->configure) {
		err = tegra->ops->configure(link);
		if (err < 0) {
			DRM_ERROR("failed to configure DP link: %d\n", err);
			return err;
		}
	}

	return drm_dp_link_configure(tegra->aux, link);
}

/**
 * DOC: Link training
 *
 * These functions contain common logic and helpers to implement DisplayPort
 * link training.
 */

/**
 * drm_dp_link_train_init() - initialize DisplayPort link training state
 * @train: DisplayPort link training state
 */
void drm_dp_link_train_init(struct drm_dp_link_train *train)
{
	struct drm_dp_link_train_set *request = &train->request;
	struct drm_dp_link_train_set *adjust = &train->adjust;
	unsigned int i;

	for (i = 0; i < 4; i++) {
		request->voltage_swing[i] = 0;
		adjust->voltage_swing[i] = 0;

		request->pre_emphasis[i] = 0;
		adjust->pre_emphasis[i] = 0;

		request->post_cursor[i] = 0;
		adjust->post_cursor[i] = 0;
	}

	train->pattern = DP_TRAINING_PATTERN_DISABLE;
	train->clock_recovered = false;
	train->channel_equalized = false;
}
EXPORT_SYMBOL(drm_dp_link_train_init);

static bool drm_dp_link_train_valid(const struct drm_dp_link_train *train)
{
	return train->clock_recovered && train->channel_equalized;
}

static int drm_dp_link_apply_training(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	struct drm_dp_link_train_set *request = &tegra->train.request;
	unsigned int lanes = link->lanes, *vs, *pe, *pc, i;
	struct drm_dp_aux *aux = tegra->aux;
	u8 values[4], pattern = 0;
	int err;

	err = tegra->ops->apply_training(link);
	if (err < 0) {
		DRM_ERROR("failed to apply link training: %d\n", err);
		return err;
	}

	vs = request->voltage_swing;
	pe = request->pre_emphasis;
	pc = request->post_cursor;

	/* write currently selected voltage-swing and pre-emphasis levels */
	for (i = 0; i < lanes; i++)
		values[i] = DP_TRAIN_VOLTAGE_SWING_LEVEL(vs[i]) |
			    DP_TRAIN_PRE_EMPHASIS_LEVEL(pe[i]);

	err = drm_dp_dpcd_write(aux, DP_TRAINING_LANE0_SET, values, lanes);
	if (err < 0) {
		DRM_ERROR("failed to set training parameters: %d\n", err);
		return err;
	}

	/* write currently selected post-cursor level (if supported) */
	if (link->revision >= 0x12 && link->rate == 540000) {
		values[0] = values[1] = 0;

		for (i = 0; i < lanes; i++)
			values[i / 2] |= DP_LANE_POST_CURSOR(i, pc[i]);

		err = drm_dp_dpcd_write(aux, DP_TRAINING_LANE0_1_SET2, values,
					DIV_ROUND_UP(lanes, 2));
		if (err < 0) {
			DRM_ERROR("failed to set post-cursor: %d\n", err);
			return err;
		}
	}

	/* write link pattern */
	if (tegra->train.pattern != DP_TRAINING_PATTERN_DISABLE)
		pattern |= DP_LINK_SCRAMBLING_DISABLE;

	pattern |= tegra->train.pattern;

	err = drm_dp_dpcd_writeb(aux, DP_TRAINING_PATTERN_SET, pattern);
	if (err < 0) {
		DRM_ERROR("failed to set training pattern: %d\n", err);
		return err;
	}

	return 0;
}

static void drm_dp_link_train_wait(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	unsigned long min = 0;

	if (link->aux_rd_interval == 0) {
		switch (tegra->train.pattern) {
		case DP_TRAINING_PATTERN_1:
			min = 100;
			break;

		case DP_TRAINING_PATTERN_2:
		case DP_TRAINING_PATTERN_3:
			min = 400;
			break;

		default:
			break;
		}
	} else {
		min = link->aux_rd_interval;
	}

	if (min > 0)
		usleep_range(min, 2 * min);
}

static void drm_dp_link_get_adjustments(struct drm_dp_link *link,
					u8 status[DP_LINK_STATUS_SIZE])
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	struct drm_dp_link_train_set *adjust = &tegra->train.adjust;
	unsigned int i;

	for (i = 0; i < link->lanes; i++) {
		adjust->voltage_swing[i] =
			drm_dp_get_adjust_request_voltage(status, i) >>
				DP_TRAIN_VOLTAGE_SWING_SHIFT;

		adjust->pre_emphasis[i] =
			drm_dp_get_adjust_request_pre_emphasis(status, i) >>
				DP_TRAIN_PRE_EMPHASIS_SHIFT;

		adjust->post_cursor[i] =
			drm_dp_get_adjust_request_post_cursor(status, i);
	}
}

static void drm_dp_link_train_adjust(struct drm_dp_link_train *train)
{
	struct drm_dp_link_train_set *request = &train->request;
	struct drm_dp_link_train_set *adjust = &train->adjust;
	unsigned int i;

	for (i = 0; i < 4; i++)
		if (request->voltage_swing[i] != adjust->voltage_swing[i])
			request->voltage_swing[i] = adjust->voltage_swing[i];

	for (i = 0; i < 4; i++)
		if (request->pre_emphasis[i] != adjust->pre_emphasis[i])
			request->pre_emphasis[i] = adjust->pre_emphasis[i];

	for (i = 0; i < 4; i++)
		if (request->post_cursor[i] != adjust->post_cursor[i])
			request->post_cursor[i] = adjust->post_cursor[i];
}

static int drm_dp_link_recover_clock(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	u8 status[DP_LINK_STATUS_SIZE];
	int err;

	err = drm_dp_link_apply_training(link);
	if (err < 0)
		return err;

	drm_dp_link_train_wait(link);

	err = drm_dp_dpcd_read_link_status(tegra->aux, status);
	if (err < 0) {
		DRM_ERROR("failed to read link status: %d\n", err);
		return err;
	}

	if (!drm_dp_clock_recovery_ok(status, link->lanes))
		drm_dp_link_get_adjustments(link, status);
	else
		tegra->train.clock_recovered = true;

	return 0;
}

static int drm_dp_link_clock_recovery(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	unsigned int repeat;
	int err;

	/* start clock recovery using training pattern 1 */
	tegra->train.pattern = DP_TRAINING_PATTERN_1;

	for (repeat = 1; repeat < 5; repeat++) {
		err = drm_dp_link_recover_clock(link);
		if (err < 0) {
			DRM_ERROR("failed to recover clock: %d\n", err);
			return err;
		}

		drm_dp_link_train_adjust(&tegra->train);

		if (tegra->train.clock_recovered)
			break;
	}

	return 0;
}

static int drm_dp_link_equalize_channel(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	struct drm_dp_aux *aux = tegra->aux;
	u8 status[DP_LINK_STATUS_SIZE];
	int err;

	err = drm_dp_link_apply_training(link);
	if (err < 0)
		return err;

	drm_dp_link_train_wait(link);

	err = drm_dp_dpcd_read_link_status(aux, status);
	if (err < 0) {
		DRM_ERROR("failed to read link status: %d\n", err);
		return err;
	}

	if (!drm_dp_clock_recovery_ok(status, link->lanes)) {
		DRM_ERROR("clock recovery lost while equalizing channel\n");
		tegra->train.clock_recovered = false;
		return 0;
	}

	if (!drm_dp_channel_eq_ok(status, link->lanes))
		drm_dp_link_get_adjustments(link, status);
	else
		tegra->train.channel_equalized = true;

	return 0;
}

static int drm_dp_link_channel_equalization(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	unsigned int repeat;
	int err;

	/* start channel equalization using pattern 2 or 3 */
	if (link->caps.tps3_supported)
		tegra->train.pattern = DP_TRAINING_PATTERN_3;
	else
		tegra->train.pattern = DP_TRAINING_PATTERN_2;

	for (repeat = 1; repeat < 5; repeat++) {
		err = drm_dp_link_equalize_channel(link);
		if (err < 0) {
			DRM_ERROR("failed to equalize channel: %d\n", err);
			return err;
		}

		drm_dp_link_train_adjust(&tegra->train);

		if (tegra->train.channel_equalized)
			break;
	}

	return 0;
}

static int drm_dp_link_downgrade(struct drm_dp_link *link)
{
	switch (link->rate) {
	case 162000:
		return -EINVAL;

	case 270000:
		link->rate = 162000;
		break;

	case 540000:
		link->rate = 270000;
		return 0;
	}

	return 0;
}

static void drm_dp_link_train_disable(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	int err;

	tegra->train.pattern = DP_TRAINING_PATTERN_DISABLE;

	err = drm_dp_link_apply_training(link);
	if (err < 0)
		DRM_ERROR("failed to disable link training: %d\n", err);
}

static int drm_dp_link_train_full(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	int err;

retry:
	DRM_DEBUG_KMS("full-training link: %u lane%s at %u MHz\n",
		      link->lanes, (link->lanes > 1) ? "s" : "",
		      link->rate / 100);

	err = __drm_dp_link_configure(link);
	if (err < 0) {
		DRM_ERROR("failed to configure DP link: %d\n", err);
		return err;
	}

	err = drm_dp_link_clock_recovery(link);
	if (err < 0) {
		DRM_ERROR("clock recovery failed: %d\n", err);
		goto out;
	}

	if (!tegra->train.clock_recovered) {
		DRM_ERROR("clock recovery failed, downgrading link\n");

		err = drm_dp_link_downgrade(link);
		if (err < 0)
			goto out;

		goto retry;
	}

	DRM_DEBUG_KMS("clock recovery succeeded\n");

	err = drm_dp_link_channel_equalization(link);
	if (err < 0) {
		DRM_ERROR("channel equalization failed: %d\n", err);
		goto out;
	}

	if (!tegra->train.channel_equalized) {
		DRM_ERROR("channel equalization failed, downgrading link\n");

		err = drm_dp_link_downgrade(link);
		if (err < 0)
			goto out;

		goto retry;
	}

	DRM_DEBUG_KMS("channel equalization succeeded\n");

out:
	drm_dp_link_train_disable(link);
	return err;
}

static int drm_dp_link_train_fast(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	u8 status[DP_LINK_STATUS_SIZE];
	int err;

	DRM_DEBUG_KMS("fast-training link: %u lane%s at %u MHz\n",
		      link->lanes, (link->lanes > 1) ? "s" : "",
		      link->rate / 100);

	err = __drm_dp_link_configure(link);
	if (err < 0) {
		DRM_ERROR("failed to configure DP link: %d\n", err);
		return err;
	}

	/* transmit training pattern 1 for 500 microseconds */
	tegra->train.pattern = DP_TRAINING_PATTERN_1;

	err = drm_dp_link_apply_training(link);
	if (err < 0)
		goto out;

	usleep_range(500, 1000);

	/* transmit training pattern 2 or 3 for 500 microseconds */
	if (link->caps.tps3_supported)
		tegra->train.pattern = DP_TRAINING_PATTERN_3;
	else
		tegra->train.pattern = DP_TRAINING_PATTERN_2;

	err = drm_dp_link_apply_training(link);
	if (err < 0)
		goto out;

	usleep_range(500, 1000);

	err = drm_dp_dpcd_read_link_status(tegra->aux, status);
	if (err < 0) {
		DRM_ERROR("failed to read link status: %d\n", err);
		goto out;
	}

	if (!drm_dp_clock_recovery_ok(status, link->lanes)) {
		DRM_ERROR("clock recovery failed\n");
		err = -EIO;
	}

	if (!drm_dp_channel_eq_ok(status, link->lanes)) {
		DRM_ERROR("channel equalization failed\n");
		err = -EIO;
	}

out:
	drm_dp_link_train_disable(link);
	return err;
}

/**
 * drm_dp_link_train() - perform DisplayPort link training
 * @link: a DP link object
 *
 * Uses the context stored in the DP link object to perform link training. It
 * is expected that drivers will call drm_dp_link_probe() to obtain the link
 * capabilities before performing link training.
 *
 * If the sink supports fast link training (no AUX CH handshake) and valid
 * training settings are available, this function will try to perform fast
 * link training and fall back to full link training on failure.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int drm_dp_link_train(struct drm_dp_link *link)
{
	struct drm_dp_link_tegra *tegra = to_drm_dp_link_tegra(link);
	int err;

	if (link->caps.fast_training) {
		if (drm_dp_link_train_valid(&tegra->train)) {
			err = drm_dp_link_train_fast(link);
			if (err < 0)
				DRM_ERROR("fast link training failed: %d\n",
					  err);
			else
				return 0;
		} else {
			DRM_DEBUG_KMS("training parameters not available\n");
		}
	} else {
		DRM_DEBUG_KMS("fast link training not supported\n");
	}

	err = drm_dp_link_train_full(link);
	if (err < 0)
		DRM_ERROR("full link training failed: %d\n", err);

	return err;
}
EXPORT_SYMBOL(drm_dp_link_train);
