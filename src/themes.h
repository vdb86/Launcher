// themes.h - XMB gradient color themes, values 1:1 from RetroArch xmb.c
// (menu/drivers/xmb.c :: xmb_gradient_ident, GPLv3).
//
// Each theme = 4 RGBA corners in RETROARCH's native order (from the default vertex layout,
// "vertex coords are specified bottom-up: BL BR TL TR"):
//   c[0] = bottom-left   c[1] = bottom-right   c[2] = top-left   c[3] = top-right
// UpdateGradientVB() in main.cpp maps them accordingly, so the on-screen orientation matches
// RetroArch exactly (do NOT flip these vertically here).
//
// This is the full RetroArch XMB set (21 themes), 1:1 with upstream. No custom themes - the
// Custom Colour Ribbon carries its own colour, so a dedicated black theme is not needed.
#pragma once

struct GradientTheme
{
    const char* name;
    float c[4][4]; // 4 corners (BL, BR, TL, TR), RGBA
};

// helper: /255
#define R(x) ((x) / 255.0f)

static const GradientTheme kThemes[] = {
    { "Legacy Red", {
        { R(171), R(70), R(59), 1 }, { R(171), R(70), R(59), 1 },
        { R(190), R(80), R(69), 1 }, { R(190), R(80), R(69), 1 } } },
    { "Dark Purple", {
        { R(20), R(13), R(20), 1 }, { R(20), R(13), R(20), 1 },
        { R(92), R(44), R(92), 1 }, { R(148), R(90), R(148), 1 } } },
    { "Midnight Blue", {
        { R(44), R(62), R(80), 1 }, { R(44), R(62), R(80), 1 },
        { R(44), R(62), R(80), 1 }, { R(44), R(62), R(80), 1 } } },
    { "Golden", {
        { R(174), R(123), R(44), 1 }, { R(205), R(174), R(84), 1 },
        { R(58), R(43), R(24), 1 }, { R(58), R(43), R(24), 1 } } },
    { "Electric Blue", {
        { R(1), R(2), R(67), 1 }, { R(1), R(73), R(183), 1 },
        { R(1), R(93), R(194), 1 }, { R(3), R(162), R(254), 1 } } },
    { "Apple Green", {
        { R(102), R(134), R(58), 1 }, { R(122), R(131), R(52), 1 },
        { R(82), R(101), R(35), 1 }, { R(63), R(95), R(30), 1 } } },
    { "Undersea", {
        { R(23), R(18), R(41), 1 }, { R(30), R(72), R(114), 1 },
        { R(52), R(88), R(110), 1 }, { R(69), R(125), R(140), 1 } } },
    { "Volcanic Red", {
        { 1.0f, 0.0f, 0.1f, 1 }, { 1.0f, 0.1f, 0.0f, 1 },
        { 0.1f, 0.0f, 0.1f, 1 }, { 0.1f, 0.0f, 0.1f, 1 } } },
    { "Dark", {
        { 0.05f, 0.05f, 0.05f, 1 }, { 0.05f, 0.05f, 0.05f, 1 },
        { 0.05f, 0.05f, 0.05f, 1 }, { 0.05f, 0.05f, 0.05f, 1 } } },
    { "Light", {
        { 0.50f, 0.50f, 0.50f, 1 }, { 0.50f, 0.50f, 0.50f, 1 },
        { 0.50f, 0.50f, 0.50f, 1 }, { 0.50f, 0.50f, 0.50f, 1 } } },
    { "Morning Blue", {
        { R(221), R(241), R(254), 1 }, { R(135), R(206), R(250), 1 },
        { 0.7f, 0.7f, 0.7f, 1 }, { R(170), R(200), R(252), 1 } } },
    { "Sunbeam", {
        { R(20), R(13), R(20), 1 }, { R(30), R(72), R(114), 1 },
        { 0.7f, 0.7f, 0.7f, 1 }, { 0.1f, 0.0f, 0.1f, 1 } } },
    { "Lime Green", {
        { R(209), R(255), R(82), 1 }, { R(146), R(232), R(66), 1 },
        { R(82), R(101), R(35), 1 }, { R(63), R(95), R(30), 1 } } },
    { "Midgar", {
        { R(255), R(0), R(0), 1 }, { R(0), R(0), R(255), 1 },
        { R(0), R(255), R(0), 1 }, { R(32), R(32), R(32), 1 } } },
    { "Pikachu Yellow", {
        { R(63), R(63), R(1), 1 }, { R(174), R(174), R(1), 1 },
        { R(191), R(194), R(1), 1 }, { R(254), R(221), R(3), 1 } } },
    { "GameCube Purple", {
        { R(40), R(20), R(91), 1 }, { R(160), R(140), R(211), 1 },
        { R(107), R(92), R(177), 1 }, { R(84), R(71), R(132), 1 } } },
    { "Famicom Red", {
        { R(255), R(191), R(171), 1 }, { R(119), R(49), R(28), 1 },
        { R(148), R(10), R(36), 1 }, { R(206), R(126), R(110), 1 } } },
    { "Flaming Hot", {
        { R(231), R(53), R(53), 1 }, { R(242), R(138), R(97), 1 },
        { R(236), R(97), R(76), 1 }, { R(255), R(125), R(3), 1 } } },
    { "Ice Cold", {
        { R(66), R(183), R(229), 1 }, { R(29), R(164), R(255), 1 },
        { R(176), R(255), R(247), 1 }, { R(174), R(240), R(255), 1 } } },
    { "Gray Dark", {
        { R(16), R(16), R(16), 1 }, { R(16), R(16), R(16), 1 },
        { R(16), R(16), R(16), 1 }, { R(16), R(16), R(16), 1 } } },
    { "Gray Light", {
        { R(32), R(32), R(32), 1 }, { R(32), R(32), R(32), 1 },
        { R(32), R(32), R(32), 1 }, { R(32), R(32), R(32), 1 } } },
};

#undef R
static const int kThemeCount = (int)(sizeof(kThemes) / sizeof(kThemes[0]));
