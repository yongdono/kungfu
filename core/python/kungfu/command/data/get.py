import click
from kungfu.command.data import data, pass_ctx_from_parent
from kungfu_extensions import EXTENSION_REGISTRY_DATA
from pykungfu import longfist
from pykungfu import yijinjing as yjj


@data.command()
@click.option('-s', '--source', required=True, type=click.Choice(EXTENSION_REGISTRY_DATA.names()), help='data source')
@click.pass_context
def get(ctx, source):
    pass_ctx_from_parent(ctx)
    # config = yjj.location(yjj.mode.LIVE, yjj.category.MD, source, source, ctx.runtime_locator).to(longfist.types.Config())
    # config = yjj.profile(ctx.runtime_locator).get(config)
    ext = EXTENSION_REGISTRY_DATA.get_extension(source)(ctx)
    ext.run()
