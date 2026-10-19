CREATE TABLE IF NOT EXISTS `mod_discord_guild_map` (
    `guild_id`    INT UNSIGNED NOT NULL COMMENT 'guild.guildid',
    `webhook_url` VARCHAR(255) NOT NULL COMMENT 'Discord webhook URL for the guild channel',
    PRIMARY KEY (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
