ALTER TABLE `mod_discord_guild_map`
    ADD COLUMN `discord_channel_id` BIGINT UNSIGNED NULL DEFAULT NULL AFTER `webhook_url`;

CREATE TABLE IF NOT EXISTS `mod_discord_relay_queue` (
    `id`              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `wow_guild_id`    INT UNSIGNED NOT NULL,
    `discord_user_id` BIGINT UNSIGNED NOT NULL,
    `display_name`    VARCHAR(64) NOT NULL,
    `message`         VARCHAR(2000) NOT NULL,
    `created_at`      TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `processed`       TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    KEY `idx_pending` (`processed`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
