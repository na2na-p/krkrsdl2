package pw.uyjulian.krkrsdl2;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;

import org.libsdl.app.SDLActivity;

public class KirikiriSDL2Activity extends SDLActivity {

    // Select-list dialog

    /** Result of the current select-list dialog. Also used for blocking the calling thread. */
    private static final int[] selectListSelection = new int[1];
    /**
     * Set alongside a notify() on selectListSelection, and only there --
     * guards against a spurious wakeup returning from wait() before
     * onDismiss (or the show()-failure fallback below) actually ran.
     */
    private static final boolean[] selectListDone = new boolean[1];

    /**
     * This method is called by SDL using JNI.
     * Shows an Android-native, scrollable, cancelable list dialog on the UI
     * thread and blocks the calling thread until an item is tapped or the
     * dialog is dismissed (back key, outside tap). This replaces
     * SDLActivity's own messagebox for list selection: that dialog lays
     * buttons out in a single non-scrolling horizontal row and hard-codes
     * setCancelable(false), which on-device pushes a handful of items'
     * cancel button off-screen and unreachable.
     * @param title dialog title
     * @param items list item labels
     * @return the tapped 0-based index, or -1 if cancelled
     */
    public static int showSelectList(final String title, final String[] items) {
        selectListSelection[0] = -1;
        selectListDone[0] = false;

        final Activity activity = (Activity) getContext();
        if (activity == null) {
            return -1;
        }

        // trigger Dialog creation on UI thread

        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                // Building/showing the dialog can throw (e.g. the activity
                // is finishing and the window token is no longer valid) --
                // unlike SDLActivity's own messagebox, this must not let
                // that exception skip the notify(), or the calling thread
                // (already inside wait()) would hang forever with no
                // dialog on screen to eventually dismiss it.
                try {
                    AlertDialog.Builder builder = new AlertDialog.Builder(activity);
                    builder.setTitle(title);
                    builder.setItems(items, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            selectListSelection[0] = which;
                        }
                    });
                    builder.setCancelable(true);
                    builder.setOnCancelListener(new DialogInterface.OnCancelListener() {
                        @Override
                        public void onCancel(DialogInterface dialog) {
                            selectListSelection[0] = -1;
                        }
                    });

                    AlertDialog dialog = builder.create();
                    dialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
                        @Override
                        public void onDismiss(DialogInterface unused) {
                            synchronized (selectListSelection) {
                                selectListDone[0] = true;
                                selectListSelection.notify();
                            }
                        }
                    });
                    dialog.show();
                } catch (Exception ex) {
                    ex.printStackTrace();
                    synchronized (selectListSelection) {
                        selectListSelection[0] = -1;
                        selectListDone[0] = true;
                        selectListSelection.notify();
                    }
                }
            }
        });

        // block the calling thread

        synchronized (selectListSelection) {
            while (!selectListDone[0]) {
                try {
                    selectListSelection.wait();
                } catch (InterruptedException ex) {
                    ex.printStackTrace();
                    return -1;
                }
            }
        }

        // return selected value

        return selectListSelection[0];
    }
}
