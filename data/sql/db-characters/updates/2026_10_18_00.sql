ALTER TABLE `mod_discord_guild_map`
    ADD COLUMN `announce_login`       TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `webhook_url`,
    ADD COLUMN `announce_logout`      TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `announce_login`,
    ADD COLUMN `announce_level`       TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `announce_logout`,
    ADD COLUMN `announce_achievement` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `announce_level`,
    ADD COLUMN `announce_bosskill`    TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `announce_achievement`;
