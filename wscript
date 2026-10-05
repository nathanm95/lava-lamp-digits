import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    ctx.load('pebble_sdk')

    # Verification affordances, never ship a bundle built with these set.
    # `LAVA_DEBUG=1 pebble build` draws frame time, fps and an input counter
    # on screen, because screendumps are the only output channel that reliably
    # works in the emulator. `LAVA_HOLD=1` keeps the digits formed forever so
    # the formed time can be captured at leisure.
    for flag in ('LAVA_DEBUG', 'LAVA_HOLD'):
        if os.environ.get(flag):
            for env in ctx.all_envs.values():
                env.append_value('DEFINES', '%s=1' % flag)

    # `LAVA_SHOW_TIME=1008 pebble build` always forms 10:08, for store
    # screenshots. The emulator follows the host clock and ignores attempts
    # to set it, so this is the only way to pick the time on screen.
    show_time = os.environ.get('LAVA_SHOW_TIME')
    if show_time:
        for env in ctx.all_envs.values():
            env.append_value('DEFINES', 'LAVA_SHOW_TIME=%d' % int(show_time))


def build(ctx):
    ctx.load('pebble_sdk')

    binaries = []
    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'),
                      target=app_elf, bin_type='app')
        binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json']),
                   js_entry_file='src/pkjs/index.js')
