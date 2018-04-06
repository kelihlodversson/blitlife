#include "blitter.h"
#include <osbind.h>
#include <time.h>
#include <gem.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
void blitlife(MFDB* image);

MFDB screen = { NULL, 0,0,0, 0,0,0,0,0};

int app_id;
VdiHdl vdi_handle;
int16_t cellw, cellh, chboxw, chboxh;

typedef struct window_struct {
    int16_t handle;
    struct window_struct* next;
    char title[256];
    MFDB* image;
    GRECT min_size;
} window_t;

typedef struct message_struct {
    int16_t msg_type;
    int16_t sender;
    int16_t size;
    union {
        int16_t data[4];
        struct {
            int16_t handle;
            GRECT rect;
        } ;
    };
} message_t;

window_t* window_list;

window_t* get_window(int16_t handle)
{
    // Loop through the window list to get window info.
    for (window_t* cur = window_list; cur; cur=cur->next)
    {
        if (cur->handle == handle)
            return cur;
    }
    return NULL;
}

MFDB* mfdb_create(int16_t width, int16_t height, int16_t planes)
{
    int16_t wdwidth = width >> 4;
    if(width & 15) // in case width is not divisible by 16
    {
        wdwidth++; // add an extra word per line
    }

    // Allocate the struct and a buffer in one go
    MFDB* mfdb = calloc(1, sizeof(MFDB));
    mfdb->fd_addr = calloc(wdwidth * height * planes, sizeof(int16_t));
    mfdb->fd_w = width;
    mfdb->fd_h = height;
    mfdb->fd_wdwidth = wdwidth;
    mfdb->fd_stand = 0;
    mfdb->fd_nplanes = planes;
    return mfdb;
}

void mfdb_delete(MFDB* mfdb)
{
    free(mfdb->fd_addr);
    free(mfdb);
}


void handle_move_resize_event(message_t* message)
{
    window_t* window = get_window(message->handle);
    if (window)
    {
        if (message->rect.g_w < window->min_size.g_w)
        {
            message->rect.g_w = window->min_size.g_w;
        }

        if (message->rect.g_h < window->min_size.g_h)
        {
            message->rect.g_h = window->min_size.g_h;
        }

        // TODO constrrain within desktop

        wind_set_grect(message->handle, WF_CURRXYWH, &message->rect);
    }
}

void handle_redraw_event(message_t* message)
{
    window_t* window = get_window(message->handle);
    if (window)
    {
        GRECT wrect, work_area, damage_area;
        wind_get_grect(window->handle, WF_WORKXYWH, &work_area);
        int16_t w_corners[4];
        grect_to_array(&work_area, w_corners);

        int16_t colors[2] = {1,0};
        int16_t coords[8] = {0,0,window->image->fd_w-1, window->image->fd_h-1,
            w_corners[0], w_corners[1], w_corners[2], w_corners[3]};
        damage_area = message->rect;
        rc_intersect(&work_area, &damage_area);

        graf_mouse(M_OFF, NULL);
        wind_update(BEG_UPDATE);
        for( wind_get_grect(window->handle, WF_FIRSTXYWH, &wrect);
             wrect.g_w && wrect.g_h ;
             wind_get_grect(window->handle, WF_NEXTXYWH, &wrect)
        )
        {
            if (rc_intersect(&message->rect, &wrect))
            {
                int16_t corners[4];
                grect_to_array(&wrect, corners);
                vs_clip(vdi_handle, true, corners);
                vsf_interior(vdi_handle, 0);
                vr_recfl(vdi_handle, w_corners);
                vrt_cpyfm(vdi_handle, MD_REPLACE, coords, window->image, &screen, colors);
                vs_clip(vdi_handle, false, corners);
            }
        }
        wind_update(END_UPDATE);
        graf_mouse(M_ON, NULL);
    }
}

VdiHdl init_app()
{
    int16_t input[12] = {1,1,1,1,1,1,1,1,1,1,2,1};
    int16_t output[57];

    app_id = appl_init();
    input[0] = Getrez()+2;
    vdi_handle = graf_handle(&cellw, &cellh, &chboxw, &chboxh);
    v_opnvwk(input, &vdi_handle, output);
}

int count_windows()
{
    int count = 0;
    // Loop through the window list to get window info.
    for (window_t* cur = window_list; cur; cur=cur->next)
    {
        count++;
    }
    return count;
}

void copy_opaque(int16_t mode, GRECT* source_rect, GRECT* dest_rect, MFDB* source, MFDB* dest)
{
    int16_t coords[8];
    grect_to_array(source_rect, coords);
    grect_to_array(dest_rect, &(coords[4]));
    vro_cpyfm(vdi_handle, mode, coords, source, dest);
}

int load_pattern(MFDB* source)
{
    GRECT desk_rect, window_rect;
    window_t* desk = get_window(DESKTOP_HANDLE);
    int window_count = count_windows();

    // pad image with at least a single pixel on each side (and pad width to multiple of 16)
    short img_width = (source->fd_w | 15) + 1;
    short img_height = source->fd_h + 2;
    img_width = img_width < 128 ? 128 : img_width;
    img_height = img_height < 128 ? 128 : img_height;
    MFDB* image = mfdb_create(img_width, img_height, source->fd_nplanes);

    GRECT source_rect = {
        0,0,source->fd_w,source->fd_h
    };
    GRECT dest_rect = {
        (img_width-source->fd_w)/2,(img_height-source->fd_h)/2,source->fd_w,source->fd_h
    };

    copy_opaque(S_ONLY, &source_rect, &dest_rect, source, image);


    wind_get_grect(desk->handle, WF_WORKXYWH, &desk_rect);
    GRECT inner_rect = {desk_rect.g_x, desk_rect.g_y+chboxh, img_width, img_height};
    wind_calc_grect(WC_BORDER, NAME|CLOSER|MOVER, &inner_rect, &window_rect);
    window_rect.g_x = desk_rect.g_x + 20+ (window_count-1)*(window_rect.g_w+5);
    window_rect.g_y = desk_rect.g_y+10;

    window_t* new_window = calloc(1, sizeof(window_t));
    new_window->next = desk->next;
    new_window->image = image;
    desk->next = new_window;

    snprintf(new_window->title, sizeof(new_window->title), "Life %d", (int)window_count);

    new_window->min_size.g_w = chboxw;
    new_window->min_size.g_h = chboxh*2;
    new_window->handle = wind_create_grect(NAME|CLOSER|MOVER, &desk_rect);
    wind_set_str(new_window->handle, WF_NAME, new_window->title);
    wind_open_grect(new_window->handle, &window_rect);
}

// On each tick cycle through one image at a time and process it
static window_t* timer_window = NULL;
void handle_timer_event()
{
    // Start at the head of the window list
    // will also loop back at the end of the list
    if (!timer_window)
        timer_window = window_list;

    // Skip windows without an image
    while(timer_window && !timer_window->image)
        timer_window = timer_window->next;

    // If there is a current window with an image,
    if(timer_window)
    {
        //graf_mouse(HOURGLASS, NULL);
        // process it and skip to the next window
        blitlife(timer_window->image);
        // Tell the window to refresh:
        message_t message;
        message.msg_type = WM_REDRAW;
        message.sender = app_id;
        message.size = 0;
        message.handle = timer_window->handle;
        wind_get_grect(message.handle, WF_CURRXYWH, &message.rect);
        appl_write(app_id, sizeof(message_t), (short*)&message );

        // Fetch next window in the chain for the next update
        timer_window = timer_window->next;
        //graf_mouse(ARROW, NULL);
    }

}

void handle_close_window_event(int16_t handle)
{
    window_t* prev = NULL;
    for(window_t* cur = window_list; cur; prev=cur, cur=cur->next)
    {
        // Deallocate and remove window from list
        if (cur->handle == handle)
        {
            // ensure the timer doesn't attempt to update the window
            if(cur == timer_window)
            {
                timer_window = NULL;
            }

            cur->handle = NIL;
            if(prev)
            {
                prev->next = cur->next;
            }
            else
            {
                window_list = cur->next;
            }

            wind_close(handle);
            wind_delete(handle);
            cur->next = NULL;
            mfdb_delete(cur->image);
            free(cur);
        }
    }
}

static const uint16_t acorn_data[] = {
    0b0000000000000000,
    0b0000100000000000,
    0b0000001000000000,
    0b0001100111000000,
    0b0000000000000000,
};

MFDB acorn = {
    (void*)acorn_data,
    16, 4, // WxH in pixels
    1,    // words per line
    0,    // device specific format
    1,    // planes
    0,0,0 // reserved
};


static const uint16_t babbling_brook_data[] = {
    0b0000000100000000,
    0b0000011100001100,
    0b0000100011001000,
    0b0100101100101000,
    0b1010100001100110,
    0b0110011000010101,
    0b0001010011010010,
    0b0001001100010000,
    0b0011000011100000,
    0b0000000010000000,
};

MFDB babbling_brook = {
    (void*)babbling_brook_data,
    16, 10, // WxH in pixels
    1,    // words per line
    0,    // device specific format
    1,    // planes
    0,0,0 // reserved
};


static const uint16_t gosper_glider_gun_data[] = {

    0b0000000000000000, 0b0000000000000010, 0b0000000000000000,
    0b0000000000000000, 0b0000000000001010, 0b0000000000000000,
    0b0000000000000000, 0b0011000000110000, 0b0000000011000000,
    0b0000000000000000, 0b0100010000110000, 0b0000000011000000,
    0b0000001100000000, 0b1000001000110000, 0b0000000000000000,
    0b0000001100000000, 0b1000101100001010, 0b0000000000000000,
    0b0000000000000000, 0b1000001000000010, 0b0000000000000000,
    0b0000000000000000, 0b0100010000000000, 0b0000000000000000,
    0b0000000000000000, 0b0011000000000000, 0b0000000000000000,
};



MFDB gosper_glider_gun = {
    (void*)gosper_glider_gun_data,
    48, 9, // WxH in pixels
    3,    // words per line
    0,    // device specific format
    1,    // planes
    0,0,0 // reserved
};


int main()
{
    int16_t status;

    window_t desk = {DESKTOP_HANDLE, NULL, "DESKTOP"};
    window_list = &desk;
    wind_set_str(desk.handle, WF_NAME, desk.title);

    init_app();
    load_pattern(&acorn);
    load_pattern(&babbling_brook);
    load_pattern(&gosper_glider_gun);

    message_t message;
    EVMULT_IN evt_in;
    EVMULT_OUT evt_out;

    evt_in.emi_flags = MU_MESAG | MU_TIMER;
    evt_in.emi_tlow = 100;
    evt_in.emi_thigh = 0;
    bool running = true;
    graf_mouse(ARROW, NULL);
    while (running)
    {
        int16_t events = evnt_multi_fast(&evt_in,(short*)&message,&evt_out);
        if(events & MU_MESAG)
        {
            switch (message.msg_type) {
                case WM_CLOSED:
                handle_close_window_event(message.handle);
                running = count_windows() > 1;
                case WM_SIZED:
                case WM_MOVED:
                handle_move_resize_event(&message);
                break;
                case WM_TOPPED:
                wind_set_str(message.handle, WF_TOP, NULL);
                break;
                case WM_BOTTOMED:
                wind_set_str(message.handle, WF_BOTTOM, NULL);
                case WM_REDRAW:
                handle_redraw_event(&message);
                break;
            }
        }
        if(events & MU_TIMER)
        {
            handle_timer_event();
        }
    }

    return appl_exit() == 0?0:1;
}
