// Clay settings page for Lava Lamp Digits.
//
// Keys must match `messageKeys` in package.json and the MESSAGE_KEY_* reads in
// src/c/settings.c. Defaults repeat set_defaults() there, so the page opens
// showing what the watch is actually doing on a first run.

module.exports = [
  {
    type: 'heading',
    defaultValue: 'Lava Lamp Digits',
    size: 1
  },
  {
    type: 'text',
    defaultValue:
      'Blobs of lava drift around the lamp and pour themselves into the ' +
      'time when you look. Tap the screen or flick your wrist to see it.'
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Colours' },
      {
        type: 'color',
        messageKey: 'LavaColor',
        label: 'Lava',
        defaultValue: 'FF5500',
        sunlight: false
      },
      {
        type: 'color',
        messageKey: 'BgColor',
        label: 'Liquid',
        defaultValue: '550055',
        allowGray: true,
        sunlight: false
      },
      {
        type: 'text',
        defaultValue:
          'The glowing centre of each blob and its soft edge are worked out ' +
          'from these two automatically.'
      }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Showing the time' },
      {
        type: 'toggle',
        messageKey: 'RaiseToShow',
        label: 'Raise your wrist to show the time',
        description:
          'Watches the accelerometer for the watch turning to face you. ' +
          'Uses a little more battery. Tapping the screen and flicking ' +
          'your wrist always work.',
        defaultValue: true
      },
      {
        type: 'select',
        messageKey: 'AutoForm',
        label: 'Also form the time on its own',
        defaultValue: '5',
        options: [
          { label: 'Every minute', value: '1' },
          { label: 'Every 5 minutes', value: '5' },
          { label: 'Every 15 minutes', value: '15' },
          { label: 'Never', value: '0' }
        ]
      }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Battery' },
      {
        type: 'select',
        messageKey: 'DriftMode',
        label: 'Keep the lava moving',
        description:
          'Animating the lava redraws the whole screen many times a second. ' +
          '"Always" uses far more battery than a normal watchface.',
        defaultValue: '0',
        options: [
          { label: 'For 30 seconds after a glance', value: '0' },
          { label: 'Always', value: '1' }
        ]
      }
    ]
  },

  { type: 'submit', defaultValue: 'Save' }
];
