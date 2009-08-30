/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file music_gui.cpp GUI for the music playback. */

#include "stdafx.h"
#include "openttd.h"
#include "fileio_func.h"
#include "music.h"
#include "music/music_driver.hpp"
#include "window_gui.h"
#include "strings_func.h"
#include "window_func.h"
#include "sound_func.h"
#include "gfx_func.h"
#include "core/math_func.hpp"
#include "core/random_func.hpp"

#include "table/strings.h"
#include "table/sprites.h"

static byte _music_wnd_cursong;
static bool _song_is_active;
static byte _cur_playlist[NUM_SONGS_PLAYLIST];



static byte _playlist_all[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 0
};

static byte _playlist_old_style[] = {
	1, 8, 2, 9, 14, 15, 19, 13, 0
};

static byte _playlist_new_style[] = {
	6, 11, 10, 17, 21, 18, 5, 0
};

static byte _playlist_ezy_street[] = {
	12, 7, 16, 3, 20, 4, 0
};

static byte * const _playlists[] = {
	_playlist_all,
	_playlist_old_style,
	_playlist_new_style,
	_playlist_ezy_street,
	msf.custom_1,
	msf.custom_2,
};

static void SkipToPrevSong()
{
	byte *b = _cur_playlist;
	byte *p = b;
	byte t;

	if (b[0] == 0) return; // empty playlist

	do p++; while (p[0] != 0); // find the end

	t = *--p; // and copy the bytes
	while (p != b) {
		p--;
		p[1] = p[0];
	}
	*b = t;

	_song_is_active = false;
}

static void SkipToNextSong()
{
	byte *b = _cur_playlist;
	byte t;

	t = b[0];
	if (t != 0) {
		while (b[1] != 0) {
			b[0] = b[1];
			b++;
		}
		b[0] = t;
	}

	_song_is_active = false;
}

static void MusicVolumeChanged(byte new_vol)
{
	_music_driver->SetVolume(new_vol);
}

static void DoPlaySong()
{
	char filename[MAX_PATH];
	FioFindFullPath(filename, lengthof(filename), GM_DIR,
			_origin_songs_specs[_music_wnd_cursong - 1].filename);
	_music_driver->PlaySong(filename);
}

static void DoStopMusic()
{
	_music_driver->StopSong();
}

static void SelectSongToPlay()
{
	uint i = 0;
	uint j = 0;

	memset(_cur_playlist, 0, sizeof(_cur_playlist));
	do {
		/* We are now checking for the existence of that file prior
		 * to add it to the list of available songs */
		if (FioCheckFileExists(_origin_songs_specs[_playlists[msf.playlist][i] - 1].filename, GM_DIR)) {
			_cur_playlist[j] = _playlists[msf.playlist][i];
			j++;
		}
	} while (_playlists[msf.playlist][++i] != 0 && j < lengthof(_cur_playlist) - 1);

	/* Do not shuffle when on the intro-start window, as the song to play has to be the original TTD Theme*/
	if (msf.shuffle && _game_mode != GM_MENU) {
		i = 500;
		do {
			uint32 r = InteractiveRandom();
			byte *a = &_cur_playlist[GB(r, 0, 5)];
			byte *b = &_cur_playlist[GB(r, 8, 5)];

			if (*a != 0 && *b != 0) {
				byte t = *a;
				*a = *b;
				*b = t;
			}
		} while (--i);
	}
}

static void StopMusic()
{
	_music_wnd_cursong = 0;
	DoStopMusic();
	_song_is_active = false;
	InvalidateWindowWidget(WC_MUSIC_WINDOW, 0, 9);
}

static void PlayPlaylistSong()
{
	if (_cur_playlist[0] == 0) {
		SelectSongToPlay();
		/* if there is not songs in the playlist, it may indicate
		 * no file on the gm folder, or even no gm folder.
		 * Stop the playback, then */
		if (_cur_playlist[0] == 0) {
			_song_is_active = false;
			_music_wnd_cursong = 0;
			msf.playing = false;
			return;
		}
	}
	_music_wnd_cursong = _cur_playlist[0];
	DoPlaySong();
	_song_is_active = true;

	InvalidateWindowWidget(WC_MUSIC_WINDOW, 0, 9);
}

void ResetMusic()
{
	_music_wnd_cursong = 1;
	DoPlaySong();
}

void MusicLoop()
{
	if (!msf.playing && _song_is_active) {
		StopMusic();
	} else if (msf.playing && !_song_is_active) {
		PlayPlaylistSong();
	}

	if (!_song_is_active) return;

	if (!_music_driver->IsSongPlaying()) {
		if (_game_mode != GM_MENU) {
			StopMusic();
			SkipToNextSong();
			PlayPlaylistSong();
		} else {
			ResetMusic();
		}
	}
}

enum MusicTrackSelectionWidgets {
	MTSW_CLOSE,
	MTSW_CAPTION,
	MTSW_BACKGROUND,
	MTSW_LIST_LEFT,
	MTSW_LIST_RIGHT,
	MTSW_ALL,
	MTSW_OLD,
	MTSW_NEW,
	MTSW_EZY,
	MTSW_CUSTOM1,
	MTSW_CUSTOM2,
	MTSW_CLEAR,
	MTSW_SAVE,
};

struct MusicTrackSelectionWindow : public Window {
	MusicTrackSelectionWindow(const WindowDesc *desc, WindowNumber number) : Window(desc, number)
	{
		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnPaint()
	{
		this->SetWidgetDisabledState(MTSW_CLEAR, msf.playlist <= 3);
		this->LowerWidget(MTSW_LIST_LEFT);
		this->LowerWidget(MTSW_LIST_RIGHT);
		this->DrawWidgets();

		GfxFillRect(  3, 23, 3 + 177,   23 + 191, 0);
		GfxFillRect(251, 23, 251 + 177, 23 + 191, 0);

		DrawString(this->widget[MTSW_LIST_LEFT].left + 2, this->widget[MTSW_LIST_LEFT].right - 2, 15, STR_PLAYLIST_TRACK_INDEX, TC_FROMSTRING, SA_CENTER);

		SetDParam(0, STR_MUSIC_PLAYLIST_ALL + msf.playlist);
		DrawString(this->widget[MTSW_LIST_RIGHT].left + 2, this->widget[MTSW_LIST_RIGHT].right - 2, 15, STR_PLAYLIST_PROGRAM, TC_FROMSTRING, SA_CENTER);

		for (uint i = 1; i <= NUM_SONGS_AVAILABLE; i++) {
			SetDParam(0, i);
			SetDParam(2, i);
			SetDParam(1, SPECSTR_SONGNAME);
			DrawString(this->widget[MTSW_LIST_LEFT].left + 2, this->widget[MTSW_LIST_LEFT].right - 2, 23 + (i - 1) * 6, (i < 10) ? STR_PLAYLIST_TRACK_SINGLE_DIGIT : STR_PLAYLIST_TRACK_DOUBLE_DIGIT);
		}

		for (uint i = 0; i != 6; i++) {
			DrawString(this->widget[MTSW_ALL].left + 2, this->widget[MTSW_ALL].right - 2, 45 + i * 8, STR_MUSIC_PLAYLIST_ALL + i, (i == msf.playlist) ? TC_WHITE : TC_BLACK, SA_CENTER);
		}

		DrawString(this->widget[MTSW_ALL].left + 2, this->widget[MTSW_ALL].right - 2, 45 + 8 * 6 + 16, STR_PLAYLIST_CLEAR, TC_FROMSTRING, SA_CENTER);
#if 0
		DrawString(this->widget[MTSW_SAVE].left + 2, this->widget[MTSW_SAVE].right - 2, 45 + 8 * 6 + 16 * 2, STR_PLAYLIST_SAVE, TC_FROMSTRING, SA_CENTER);
#endif

		int y = 23;
		for (const byte *p = _playlists[msf.playlist]; *p != 0; p++) {
			uint i = *p;
			SetDParam(0, i);
			SetDParam(1, SPECSTR_SONGNAME);
			SetDParam(2, i);
			DrawString(this->widget[MTSW_LIST_RIGHT].left + 2, this->widget[MTSW_LIST_RIGHT].right - 2, y, (i < 10) ? STR_PLAYLIST_TRACK_SINGLE_DIGIT : STR_PLAYLIST_TRACK_DOUBLE_DIGIT);
			y += 6;
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case MTSW_LIST_LEFT: { // add to playlist
				int y = (pt.y - 23) / 6;

				if (msf.playlist < 4) return;
				if (!IsInsideMM(y, 0, NUM_SONGS_AVAILABLE)) return;

				byte *p = _playlists[msf.playlist];
				for (uint i = 0; i != NUM_SONGS_PLAYLIST - 1; i++) {
					if (p[i] == 0) {
						p[i] = y + 1;
						p[i + 1] = 0;
						this->SetDirty();
						SelectSongToPlay();
						break;
					}
				}
			} break;

			case MTSW_LIST_RIGHT: { // remove from playlist
				int y = (pt.y - 23) / 6;

				if (msf.playlist < 4) return;
				if (!IsInsideMM(y, 0, NUM_SONGS_AVAILABLE)) return;

				byte *p = _playlists[msf.playlist];
				for (uint i = y; i != NUM_SONGS_PLAYLIST - 1; i++) {
					p[i] = p[i + 1];
				}

				this->SetDirty();
				SelectSongToPlay();
			} break;

			case MTSW_CLEAR: // clear
				_playlists[msf.playlist][0] = 0;
				this->SetDirty();
				StopMusic();
				SelectSongToPlay();
				break;

#if 0
			case MTSW_SAVE: // save
				ShowInfo("MusicTrackSelectionWndProc:save not implemented");
				break;
#endif

			case MTSW_ALL: case MTSW_OLD: case MTSW_NEW:
			case MTSW_EZY: case MTSW_CUSTOM1: case MTSW_CUSTOM2: // set playlist
				msf.playlist = widget - MTSW_ALL;
				this->SetDirty();
				InvalidateWindow(WC_MUSIC_WINDOW, 0);
				StopMusic();
				SelectSongToPlay();
				break;
		}
	}
};

static const Widget _music_track_selection_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_BLACK_CROSS,                      STR_TOOLTIP_CLOSE_WINDOW},                        // MTSW_CLOSE
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_GREY,    11,   431,     0,    13, STR_PLAYLIST_MUSIC_PROGRAM_SELECTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS},              // MTSW_CAPTION
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,     0,   431,    14,   217, 0x0,                                  STR_NULL},                                        // MTSW_BACKGROUND
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,     2,   181,    22,   215, 0x0,                                  STR_PLAYLIST_TOOLTIP_CLICK_TO_ADD_TRACK},         // MTSW_LIST_LEFT
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,   250,   429,    22,   215, 0x0,                                  STR_PLAYLIST_TOOLTIP_CLICK_TO_REMOVE_TRACK},      // MTSW_LIST_RIGHT
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    44,    51, 0x0,                                  STR_MUSIC_TOOLTIP_SELECT_ALL_TRACKS_PROGRAM},     // MTSW_ALL
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    52,    59, 0x0,                                  STR_MUSIC_TOOLTIP_SELECT_OLD_STYLE_MUSIC},        // MTSW_OLD
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    60,    67, 0x0,                                  STR_MUSIC_TOOLTIP_SELECT_NEW_STYLE_MUSIC},        // MTSW_NEW
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    68,    75, 0x0,                                  STR_MUSIC_TOOLTIP_SELECT_EZY_STREET_STYLE},       // MTSW_EZY
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    76,    83, 0x0,                                  STR_MUSIC_TOOLTIP_SELECT_CUSTOM_1_USER_DEFINED},  // MTSW_CUSTOM1
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,    84,    91, 0x0,                                  STR_MUSIC_TOOLTIP_SELECT_CUSTOM_2_USER_DEFINED},  // MTSW_CUSTOM2
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,   108,   115, 0x0,                                  STR_PLAYLIST_TOOLTIP_CLEAR_CURRENT_PROGRAM_CUSTOM1}, // MTSW_CLEAR
#if 0
{    WWT_PUSHBTN,   RESIZE_NONE,  COLOUR_GREY,   186,   245,   124,   131, 0x0,                                  STR_PLAYLIST_TOOLTIP_SAVE_MUSIC_SETTINGS},           // MTSW_SAVE
#endif
{   WIDGETS_END},
};

static const NWidgetPart _nested_music_track_selection_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY, MTSW_CLOSE),
		NWidget(WWT_CAPTION, COLOUR_GREY, MTSW_CAPTION), SetDataTip(STR_PLAYLIST_MUSIC_PROGRAM_SELECTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, MTSW_BACKGROUND),
		NWidget(NWID_HORIZONTAL), SetPIP(2, 4, 2),
			/* Left panel. */
			NWidget(NWID_VERTICAL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 8), // Space for the left heading text.
				NWidget(WWT_PANEL, COLOUR_GREY, MTSW_LIST_LEFT), SetMinimalSize(180, 194), SetDataTip(0x0, STR_PLAYLIST_TOOLTIP_CLICK_TO_ADD_TRACK), EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
			EndContainer(),
			/* Middle buttons. */
			NWidget(NWID_VERTICAL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 30), // Space above the first button from the title bar.
				NWidget(WWT_PUSHBTN, COLOUR_GREY, MTSW_ALL), SetMinimalSize(60, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_ALL_TRACKS_PROGRAM),
				NWidget(WWT_PUSHBTN, COLOUR_GREY, MTSW_OLD), SetMinimalSize(60, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_OLD_STYLE_MUSIC),
				NWidget(WWT_PUSHBTN, COLOUR_GREY, MTSW_NEW), SetMinimalSize(60, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_NEW_STYLE_MUSIC),
				NWidget(WWT_PUSHBTN, COLOUR_GREY, MTSW_EZY), SetMinimalSize(60, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_EZY_STREET_STYLE),
				NWidget(WWT_PUSHBTN, COLOUR_GREY, MTSW_CUSTOM1), SetMinimalSize(60, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_CUSTOM_1_USER_DEFINED),
				NWidget(WWT_PUSHBTN, COLOUR_GREY, MTSW_CUSTOM2), SetMinimalSize(60, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_CUSTOM_2_USER_DEFINED),
				NWidget(NWID_SPACER), SetMinimalSize(0, 16), // Space above 'clear' button
				NWidget(WWT_PUSHBTN, COLOUR_GREY, MTSW_CLEAR), SetMinimalSize(60, 8), SetDataTip(0x0, STR_PLAYLIST_TOOLTIP_CLEAR_CURRENT_PROGRAM_CUSTOM1),
#if 0
				NWidget(NWID_SPACER), SetMinimalSize(0, 8), // Space above 'save' button
				NWidget(WWT_PUSHBTN, COLOUR_GREY, MTSW_SAVE), SetMinimalSize(60, 8), SetDataTip(0x0, STR_PLAYLIST_TOOLTIP_SAVE_MUSIC_SETTINGS),
#endif
				NWidget(NWID_SPACER), SetFill(false, true),
			EndContainer(),
			/* Right panel. */
			NWidget(NWID_VERTICAL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 8), // Space for the right heading text.
				NWidget(WWT_PANEL, COLOUR_GREY, MTSW_LIST_RIGHT), SetMinimalSize(180, 194), SetDataTip(0x0, STR_PLAYLIST_TOOLTIP_CLICK_TO_REMOVE_TRACK), EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static const WindowDesc _music_track_selection_desc(
	104, 131, 432, 218, 432, 218,
	WC_MUSIC_TRACK_SELECTION, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS,
	_music_track_selection_widgets, _nested_music_track_selection_widgets, lengthof(_nested_music_track_selection_widgets)
);

static void ShowMusicTrackSelection()
{
	AllocateWindowDescFront<MusicTrackSelectionWindow>(&_music_track_selection_desc, 0);
}

enum MusicWidgets {
	MW_CLOSE,
	MW_CAPTION,
	MW_PREV,
	MW_NEXT,
	MW_STOP,
	MW_PLAY,
	MW_SLIDERS,
	MW_GAUGE,
	MW_BACKGROUND,
	MW_INFO,
	MW_SHUFFLE,
	MW_PROGRAMME,
	MW_ALL,
	MW_OLD,
	MW_NEW,
	MW_EZY,
	MW_CUSTOM1,
	MW_CUSTOM2,
};

struct MusicWindow : public Window {
	MusicWindow(const WindowDesc *desc, WindowNumber number) : Window()
	{
		this->InitNested(desc, number);
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case MW_GAUGE:
				GfxFillRect(r.left, r.top, r.right, r.bottom, 0);

				for (uint i = 0; i != 8; i++) {
					int colour = 0xD0;
					if (i > 4) {
						colour = 0xBF;
						if (i > 6) {
							colour = 0xB8;
						}
					}
					GfxFillRect(r.left, r.bottom - i * 2, r.right, r.bottom - i * 2, colour);
				}
				break;

			case MW_INFO: {
				GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, 0);
				StringID str = STR_MUSIC_TRACK_NONE;
				if (_song_is_active != 0 && _music_wnd_cursong != 0) {
					SetDParam(0, _music_wnd_cursong);
					str = (_music_wnd_cursong < 10) ? STR_MUSIC_TRACK_SINGLE_DIGIT : STR_MUSIC_TRACK_DOUBLE_DIGIT;
				}
				DrawString(r.left + 3, r.right - 3, r.top, str);
				str = STR_MUSIC_TITLE_NONE;
				if (_song_is_active != 0 && _music_wnd_cursong != 0) {
					str = STR_MUSIC_TITLE_NAME;
					SetDParam(0, SPECSTR_SONGNAME);
					SetDParam(1, _music_wnd_cursong);
				}
				DrawString(r.left, r.right, r.top + 1, str, TC_FROMSTRING, SA_CENTER);
			} break;

			case MW_SHUFFLE:
				DrawString(r.left, r.right, r.top, STR_MUSIC_SHUFFLE, (msf.shuffle ? TC_WHITE : TC_BLACK), SA_CENTER);
				break;

			case MW_PROGRAMME:
				DrawString(r.left, r.right, r.top, STR_MUSIC_PROGRAM, TC_FROMSTRING, SA_CENTER);
				break;

			case MW_ALL: case MW_OLD: case MW_NEW: case MW_EZY: case MW_CUSTOM1: case MW_CUSTOM2:
				DrawString(r.left, r.right, r.top, STR_MUSIC_PLAYLIST_ALL + (widget - MW_ALL), msf.playlist == (widget - MW_ALL) ? TC_WHITE : TC_BLACK, SA_CENTER);
				break;
		}
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();

		DrawString(108, 174, 15, STR_MUSIC_MUSIC_VOLUME, TC_FROMSTRING, SA_CENTER);
		DrawString(108, 174, 29, STR_MUSIC_MIN_MAX_RULER, TC_FROMSTRING, SA_CENTER);
		DrawString(214, 280, 15, STR_MUSIC_EFFECTS_VOLUME, TC_FROMSTRING, SA_CENTER);
		DrawString(214, 280, 29, STR_MUSIC_MIN_MAX_RULER, TC_FROMSTRING, SA_CENTER);

		DrawFrameRect(108, 23, 174, 26, COLOUR_GREY, FR_LOWERED);
		DrawFrameRect(214, 23, 280, 26, COLOUR_GREY, FR_LOWERED);

		DrawFrameRect(
			108 + msf.music_vol / 2, 22, 111 + msf.music_vol / 2, 28, COLOUR_GREY, FR_NONE
		);

		DrawFrameRect(
			214 + msf.effect_vol / 2, 22, 217 + msf.effect_vol / 2, 28, COLOUR_GREY, FR_NONE
		);
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case MW_PREV: // skip to prev
				if (!_song_is_active) return;
				SkipToPrevSong();
				break;

			case MW_NEXT: // skip to next
				if (!_song_is_active) return;
				SkipToNextSong();
				break;

			case MW_STOP: // stop playing
				msf.playing = false;
				break;

			case MW_PLAY: // start playing
				msf.playing = true;
				break;

			case MW_SLIDERS: { // volume sliders
				int x = pt.x - 88;
				if (x < 0) return;

				byte *vol = &msf.music_vol;
				if (x >= 106) {
					vol = &msf.effect_vol;
					x -= 106;
				}

				byte new_vol = min(max(x - 21, 0) * 2, 127);
				if (new_vol != *vol) {
					*vol = new_vol;
					if (vol == &msf.music_vol) MusicVolumeChanged(new_vol);
					this->SetDirty();
				}

				_left_button_clicked = false;
			} break;

			case MW_SHUFFLE: // toggle shuffle
				msf.shuffle ^= 1;
				StopMusic();
				SelectSongToPlay();
				break;

			case MW_PROGRAMME: // show track selection
				ShowMusicTrackSelection();
				break;

			case MW_ALL: case MW_OLD: case MW_NEW:
			case MW_EZY: case MW_CUSTOM1: case MW_CUSTOM2: // playlist
				msf.playlist = widget - MW_ALL;
				this->SetDirty();
				InvalidateWindow(WC_MUSIC_TRACK_SELECTION, 0);
				StopMusic();
				SelectSongToPlay();
				break;
		}
	}

#if 0
	virtual void OnTick()
	{
		this->InvalidateWidget(MW_GAUGE);
	}
#endif
};

static const NWidgetPart _nested_music_window_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY, MW_CLOSE),
		NWidget(WWT_CAPTION, COLOUR_GREY, MW_CAPTION), SetDataTip(STR_MUSIC_JAZZ_JUKEBOX_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, MW_PREV), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_SKIP_TO_PREV, STR_MUSIC_TOOLTIP_SKIP_TO_PREVIOUS_TRACK),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, MW_NEXT), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_SKIP_TO_NEXT, STR_MUSIC_TOOLTIP_SKIP_TO_NEXT_TRACK_IN_SELECTION),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, MW_STOP), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_STOP_MUSIC, STR_MUSIC_TOOLTIP_STOP_PLAYING_MUSIC),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, MW_PLAY), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_PLAY_MUSIC, STR_MUSIC_TOOLTIP_START_PLAYING_MUSIC),
		NWidget(WWT_PANEL, COLOUR_GREY, MW_SLIDERS),   SetDataTip(0x0, STR_MUSIC_TOOLTIP_DRAG_SLIDERS_TO_SET_MUSIC),
			NWidget(WWT_PANEL, COLOUR_GREY, MW_GAUGE), SetMinimalSize(16, 20), SetPadding(1, 98, 1, 98), EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetFill(true, false),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, MW_BACKGROUND),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PUSHBTN, COLOUR_GREY, MW_SHUFFLE), SetMinimalSize(50, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_TOGGLE_PROGRAM_SHUFFLE), SetPadding(6, 3, 8, 6),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 9),
				NWidget(WWT_PANEL, COLOUR_GREY, MW_INFO), SetMinimalSize(182, 9), SetFill(false, false), EndContainer(),
				NWidget(NWID_SPACER), SetFill(false, true),
			EndContainer(),
			NWidget(WWT_PUSHBTN, COLOUR_GREY, MW_PROGRAMME), SetMinimalSize(50, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SHOW_MUSIC_TRACK_SELECTION), SetPadding(6, 0, 8, 3),
			NWidget(NWID_SPACER), SetFill(true, false),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, MW_ALL), SetMinimalSize(50, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_ALL_TRACKS_PROGRAM),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, MW_OLD), SetMinimalSize(50, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_OLD_STYLE_MUSIC),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, MW_NEW), SetMinimalSize(50, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_NEW_STYLE_MUSIC),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, MW_EZY), SetMinimalSize(50, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_EZY_STREET_STYLE),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, MW_CUSTOM1), SetMinimalSize(50, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_CUSTOM_1_USER_DEFINED),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, MW_CUSTOM2), SetMinimalSize(50, 8), SetDataTip(0x0, STR_MUSIC_TOOLTIP_SELECT_CUSTOM_2_USER_DEFINED),
	EndContainer(),
};

static const WindowDesc _music_window_desc(
	0, 22, 300, 66, 300, 66,
	WC_MUSIC_WINDOW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS,
	NULL, _nested_music_window_widgets, lengthof(_nested_music_window_widgets)
);

void ShowMusicWindow()
{
	AllocateWindowDescFront<MusicWindow>(&_music_window_desc, 0);
}
