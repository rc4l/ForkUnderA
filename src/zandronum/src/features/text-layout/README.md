# features/text-layout

How text is laid out into lines, apart from the fonts that measure it.

- `computation/linebreak_compute`: `ComputeLineBreaks` finds where `V_BreakLines` breaks a text into lines no wider
  than a width. Each line comes back as places in the text itself (where it starts and stops, and the colour code it
  opens with), so finding the lines makes no string.
  - It is the same loop `V_BreakLines` always ran. A test holds it to the old loop, kept verbatim, on 20,000
    generated texts.
  - Before, every call built 128 empty `FString`s and copied each line into the returned array. Each of those strings
    touches the shared, atomic reference count of the one empty string. DnD's HUD makes about half a million
    messages in a 40-second fight, most of them empty `DeleteText` ones, each broken at least once.

## In-engine hooks

- `v_text.cpp` `V_BreakLines`: asks `ComputeLineBreaks` where the lines are, then builds each line's string once, in
  the array it returns.
