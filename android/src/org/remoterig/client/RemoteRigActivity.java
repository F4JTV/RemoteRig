package org.remoterig.client;

import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * Activite derivee de celle de Qt, uniquement pour le PTT sur touche volume.
 *
 * Qt laisse deliberement passer les touches de volume au systeme : elles ne
 * parviennent donc jamais au code C++, et un filtre d'evenements Qt ne voit
 * rien. Il faut les intercepter ici, avant que le framework ne regle le volume.
 */
public class RemoteRigActivity extends org.qtproject.qt.android.bindings.QtActivity {

    // Implementees cote C++ dans androidservice.cpp.
    public static native boolean volumePttEnabled();
    public static native void volumePtt(boolean pressed);

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // Les barres reviennent apres un balayage ou un retour d'arriere-plan :
        // on les remasque, sinon le plein ecran ne tient que jusqu'au premier
        // geste de l'operateur.
        if (hasFocus) hideSystemBars();
    }

    /** Masque barre d'etat et barre de navigation, en mode transitoire. */
    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.statusBars()
                                | WindowInsets.Type.navigationBars());
            }
        } else {
            // Anterieur a Android 11 : les anciens indicateurs de visibilite.
            final int flags = 0x00001000 | 0x00000004 | 0x00000002
                            | 0x00000100 | 0x00000200 | 0x00000400;
            getWindow().getDecorView().setSystemUiVisibility(flags);
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_VOLUME_DOWN) {
            boolean wanted = false;
            try {
                wanted = volumePttEnabled();
            } catch (UnsatisfiedLinkError e) {
                // La bibliotheque native n'est pas encore chargee : on laisse
                // le systeme faire son travail habituel.
                return super.dispatchKeyEvent(event);
            }

            if (wanted) {
                // Les repetitions d'appui sont ignorees, mais consommees :
                // sans cela le volume bougerait pendant une longue emission.
                if (event.getRepeatCount() == 0) {
                    if (event.getAction() == KeyEvent.ACTION_DOWN)      volumePtt(true);
                    else if (event.getAction() == KeyEvent.ACTION_UP)   volumePtt(false);
                }
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }
}
