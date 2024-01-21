from typing import TYPE_CHECKING
from trezor import loop, ui
from trezor.ui.layouts import interact
import trezorui2
from . import RustLayout

if TYPE_CHECKING:
    from typing import Any, Tuple


class EjectSDCardScreen(RustLayout):
    def __init__(self, layout: Any) -> None:
        super().__init__(layout=layout)

    async def sdcard_checker_task(self) -> None:
        from trezor import io

        while io.sdcard.is_present():
            await loop.sleep(100)
        raise ui.Result("eject done")

    def create_tasks(self) -> tuple[loop.AwaitableTask, ...]:
        return super().create_tasks() + (self.sdcard_checker_task(),)


async def make_user_eject_sdcard() -> None:

    await interact(
        EjectSDCardScreen(trezorui2.show_info(title="Eject SD card pls", button="")),
        "br_type",
    )
