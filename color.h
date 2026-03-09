/*
 * color.h — ANSI terminal color constants for synsh
 *
 * SynapseOS Project — GPLv2
 */
#pragma once

/* SynapseOS brand colors — cyan/electric theme */
#define COLOR_BRAND   "\033[38;5;51m"   /* electric cyan */
#define COLOR_AI      "\033[38;5;81m"   /* sky blue — AI output */
#define COLOR_CMD     "\033[38;5;222m"  /* warm gold — commands */
#define COLOR_OK      "\033[38;5;82m"   /* green */
#define COLOR_WARN    "\033[38;5;214m"  /* amber */
#define COLOR_ERR     "\033[38;5;196m"  /* red */
#define COLOR_DIM     "\033[2m"         /* dimmed */
#define COLOR_BOLD    "\033[1m"
#define COLOR_USER    "\033[38;5;159m"  /* light cyan */
#define COLOR_PATH    "\033[38;5;147m"  /* lavender */
#define COLOR_PROMPT  "\033[38;5;255m"  /* near-white */
#define COLOR_RESET   "\033[0m"
