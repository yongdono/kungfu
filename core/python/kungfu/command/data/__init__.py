import click
from pykungfu import yijinjing as yjj
from kungfu.command import kfc, pass_ctx_from_parent as pass_ctx_from_root
from kungfu.yijinjing.log import create_logger


@kfc.group(help_priority=5)
@click.help_option('-h', '--help')
@click.pass_context
def data(ctx):
    pass_ctx_from_root(ctx)
    ctx.low_latency = False
    ctx.logger = create_logger('data', ctx.log_level, ctx.console_location)
    yjj.setup_log(ctx.console_location, 'data')


def pass_ctx_from_parent(ctx):
    pass_ctx_from_root(ctx)
    ctx.logger = ctx.parent.logger
    ctx.low_latency = ctx.parent.low_latency
    ctx.runtime_dir = ctx.parent.runtime_dir
    ctx.archive_dir = ctx.parent.archive_dir
    ctx.runtime_locator = ctx.parent.runtime_locator
    ctx.config_location = ctx.parent.config_location
    ctx.console_location = ctx.parent.console_location
    ctx.index_location = ctx.parent.index_location
