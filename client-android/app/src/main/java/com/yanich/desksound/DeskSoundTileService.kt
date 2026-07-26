package com.yanich.desksound

import android.content.Intent
import android.graphics.drawable.Icon
import android.os.Build
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import androidx.annotation.RequiresApi

@RequiresApi(Build.VERSION_CODES.N)
class DeskSoundTileService : TileService() {

    override fun onStartListening() {
        super.onStartListening()
        updateTileState()
    }

    override fun onClick() {
        super.onClick()
        val tile = qsTile ?: return

        val isStreaming = AudioReceiverService.isServiceStreaming
        if (isStreaming || tile.state == Tile.STATE_ACTIVE) {
            // Stop Audio Streaming
            val intent = Intent(this, AudioReceiverService::class.java).apply {
                action = AudioReceiverService.ACTION_STOP
            }
            startService(intent)
            tile.state = Tile.STATE_INACTIVE
            tile.label = "DeskSound (Off)"
        } else {
            // Open Main Activity to Connect
            val intent = Intent(this, MainActivity::class.java).apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
            startActivityAndCollapse(intent)
        }
        tile.updateTile()
    }

    private fun updateTileState() {
        val tile = qsTile ?: return
        val isStreaming = AudioReceiverService.isServiceStreaming
        tile.state = if (isStreaming) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
        tile.label = if (isStreaming) "DeskSound (Streaming)" else "DeskSound (Off)"
        tile.icon = Icon.createWithResource(this, R.mipmap.ic_launcher)
        tile.updateTile()
    }
}
