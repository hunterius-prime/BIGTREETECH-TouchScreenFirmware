#include "ListManager.h"
#include "includes.h"

#define KEY_INDEX_PAGEUP   (LISTITEM_PER_PAGE)
#define KEY_INDEX_PAGEDOWN (KEY_INDEX_PAGEUP + 1)
#define KEY_INDEX_BACK     (KEY_INDEX_PAGEDOWN + 1)

static LISTITEMS listItems;
static LISTITEM * totalItems;

static uint16_t maxItemCount;
static uint8_t maxPageCount;
static uint16_t curPageIndex;
static uint16_t * curPageIndexSource;
static bool handleBack = true;

static void (* action_preparePage)(LISTITEMS * listItems, uint8_t index) = NULL;
static void (* action_prepareItem)(LISTITEM * item, uint16_t index, uint8_t itemPos) = NULL;

#ifdef KNOB_NAV

// Knob navigation (see LCD_Encoder.h): turning moves a focus frame over the rows, across pages, and after the last
// row onto Back; a short click selects the focused row like a touch.
static int16_t knobRow = -1;  // focused item (0 .. maxItemCount - 1), maxItemCount = Back, -1 = none

static void listViewSetCurPage(uint8_t curPage);

static int8_t knobRowPosition(int16_t row)  // position on the shown page, -1 = not shown
{
  if (row < 0)
    return -1;

  if (row >= maxItemCount)
    return KEY_INDEX_BACK;

  return (row / LISTITEM_PER_PAGE == curPageIndex) ? row % LISTITEM_PER_PAGE : -1;
}

static void knobRowShow(void)
{
  int8_t position = knobRowPosition(knobRow);

  if (position >= 0)
    menuKnobListFrame(position);
}

static void knobRowSet(int16_t row)
{
  int8_t old = knobRowPosition(knobRow);

  knobRow = row;
  menuKnobRemember(row);

  if (row < maxItemCount && row / LISTITEM_PER_PAGE != curPageIndex)  // row on another page: show that page
  {
    curPageIndex = row / LISTITEM_PER_PAGE;

    if (curPageIndexSource != NULL)
      *curPageIndexSource = curPageIndex;

    listViewSetCurPage(curPageIndex);
    menuRefreshListPage();
  }
  else if (old >= 0)
  {
    menuDrawListItem(&listItems.items[old], old);
  }

  knobRowShow();
}

static KEY_VALUES knobRowKey(void)
{
  int16_t detents = encoderPosition;
  uint8_t press = LCD_Enc_ReadPress(KNOB_LONG_PRESS_MS);

  encoderPosition = 0;

  if (detents != 0)
  {
    int16_t row;

    if (knobRow < 0)  // first turn: the first row of the shown page
      row = MIN(curPageIndex * LISTITEM_PER_PAGE, maxItemCount);
    else
      row = NOBEYOND(0, knobRow + detents, maxItemCount);

    if (row != knobRow)
      knobRowSet(row);
  }

  if (press == ENC_PRESS_SHORT)
  {
    int8_t position = knobRowPosition(knobRow);

    if (position >= 0)
      return (KEY_VALUES)position;
  }

  return KEY_IDLE;
}

#endif  // KNOB_NAV

// display page at selected index
static void listViewSetCurPage(uint8_t curPage)
{
  if (action_preparePage != NULL)
  {
    action_preparePage(&listItems, curPage);
  }
  else
  {
    for (uint8_t i = 0; i < LISTITEM_PER_PAGE; i++)
    {
      uint16_t index = curPage * LISTITEM_PER_PAGE + i;

      if (index < maxItemCount)
      {
        if (totalItems != NULL)
          listItems.items[i] = totalItems[index];

        if (action_prepareItem != NULL)
          action_prepareItem(&listItems.items[i], index, i);
      }
      else
      {
        listItems.items[i].icon = CHARICON_NULL;
      }
    }
  }

  // only 1 page or in first page, hide up button
  listItems.items[5].icon = ((maxItemCount <= LISTITEM_PER_PAGE) || (curPage == 0)) ? CHARICON_NULL : CHARICON_PAGEUP;
  // only 1 page or in last page, hide down button
  listItems.items[6].icon = ((maxItemCount <= LISTITEM_PER_PAGE) || (curPage == maxPageCount - 1)) ? CHARICON_NULL : CHARICON_PAGEDOWN;

  listItems.items[7].icon = CHARICON_BACK;

  curPageIndex = curPage;
}

void listViewCreate(LABEL title, LISTITEM * items, uint16_t maxItems, uint16_t * curPage, bool handleBackPress,
                    void (* preparePage_action)(LISTITEMS * listItems, uint8_t pageIndex),
                    void (* prepareItem_action)(LISTITEM * item, uint16_t index, uint8_t itemPos))
{
  listItems.title = title;
  totalItems = items;
  maxItemCount = maxItems;
  maxPageCount = (maxItemCount + LISTITEM_PER_PAGE - 1) / LISTITEM_PER_PAGE;
  handleBack = handleBackPress;
  action_preparePage = preparePage_action;
  action_prepareItem = prepareItem_action;
  curPageIndexSource = curPage;

  curPageIndex = (curPage == NULL) ? 0 : *curPage;

  listViewSetCurPage(curPageIndex);
  menuDrawListPage(&listItems);

  #ifdef KNOB_NAV
    knobRow = menuKnobRestore();  // back from a submenu: the row that opened it

    if (knobRow > maxItemCount || knobRowPosition(knobRow) < 0)
      knobRow = -1;

    knobRowShow();
  #endif
}

void listViewSetTitle(LABEL title)
{
  listItems.title = title;
  menuSetTitle(&listItems.title);
}

// open next page
static inline bool listViewNextPage(void)
{
  if (maxPageCount <= 1) return false;                 // only 0 or 1 page, can't goto next page
  if (curPageIndex + 1 >= maxPageCount) return false;  // already last page

  // goto next page
  curPageIndex++;

  if (curPageIndexSource != NULL)
    *curPageIndexSource = curPageIndex;

  listViewSetCurPage(curPageIndex);
  menuRefreshListPage();

  #ifdef KNOB_NAV
    if (knobRow >= 0 && knobRow < maxItemCount)  // the knob focus follows to the new page
      knobRowSet(curPageIndex * LISTITEM_PER_PAGE);
    else
      knobRowShow();
  #endif

  return true;
}

// open previous page
static inline bool listViewPreviousPage(void)
{
  if (maxPageCount <= 1) return false;  // only 0 or 1 page, can't goto previous page
  if (curPageIndex == 0) return false;  // already first page

  // goto previous page
  curPageIndex--;

  if (curPageIndexSource != NULL)
    *curPageIndexSource = curPageIndex;

  listViewSetCurPage(curPageIndex);
  menuRefreshListPage();

  #ifdef KNOB_NAV
    if (knobRow >= 0 && knobRow < maxItemCount)  // the knob focus follows to the new page
      knobRowSet(curPageIndex * LISTITEM_PER_PAGE);
    else
      knobRowShow();
  #endif

  return true;
}

void listViewRefreshPage(void)
{
  menuRefreshListPage();

  #ifdef KNOB_NAV
    knobRowShow();
  #endif
}

void listViewRefreshMenu(void)
{
  menuDrawListPage(&listItems);

  #ifdef KNOB_NAV
    knobRowShow();
  #endif
}

void listViewRefreshItem(uint16_t item)
{
  uint8_t cur_i = item % LISTITEM_PER_PAGE;

  if (item > maxItemCount)
    return;  // error index.

  if (item < curPageIndex * LISTITEM_PER_PAGE || item >= (curPageIndex + 1) * LISTITEM_PER_PAGE)
    return;  // not in cur page

  if (item < maxItemCount)
  {
    listItems.items[cur_i] = totalItems[item];

    if (action_prepareItem != NULL)
      action_prepareItem(&listItems.items[cur_i], item, cur_i);
  }
  else
  {
    listItems.items[cur_i].icon = CHARICON_NULL;
  }

  menuDrawListItem(&listItems.items[cur_i], cur_i);

  #ifdef KNOB_NAV
    if (knobRowPosition(knobRow) == cur_i)
      menuKnobListFrame(cur_i);
  #endif
}

uint8_t listViewGetCurPage(void)
{
  return curPageIndex;
}

uint16_t listViewGetSelectedIndex(void)
{
  KEY_VALUES key_num = KEY_IDLE;

  #ifdef KNOB_NAV
    key_num = knobRowKey();
  #endif

  if (key_num == KEY_IDLE)
    key_num = menuKeyGetValue();

  if (key_num < LISTITEM_PER_PAGE)
  {
    // return actual item index
    uint16_t cur_index = curPageIndex * LISTITEM_PER_PAGE + key_num;

    if (cur_index < maxItemCount)
    {
      // the shown row (totalItems is NULL for lists built by prepareItem_action)
      if (listItems.items[key_num].icon != CHARICON_NULL)
        return cur_index;
      else
        return KEY_IDLE;
    }
  }

  // check function keypress
  switch (key_num)
  {
    case KEY_DECREASE:
    case KEY_INDEX_PAGEUP:
      listViewPreviousPage();
      return KEY_PAGEUP;

    case KEY_INCREASE:
    case KEY_INDEX_PAGEDOWN:
      listViewNextPage();
      return KEY_PAGEDOWN;

    case KEY_INDEX_BACK:
      if (handleBack)
        CLOSE_MENU();
      return KEY_BACK;

    default:
      return KEY_IDLE;  // if no key is pressed and no page scrolling is requested
  }
}
