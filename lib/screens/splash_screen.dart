import 'package:flutter/material.dart';

/// Splash pertama kali app dibuka - logo+wordmark KAWAL di tengah, sebelum
/// masuk ke login/home. Ditampilkan oleh [AuthGate] di main.dart selama
/// status auth masih `loading` (cek token tersimpan) DAN durasi minimum
/// belum lewat - durasi minimum ini supaya splash tidak "kedip" sekilas
/// kalau proses cek token-nya sangat cepat.
///
/// Latar putih (bukan warna brand) karena logo-nya sendiri biru di atas
/// transparan, didesain untuk latar terang - bukan logo putih di atas warna.
class SplashScreen extends StatelessWidget {
  const SplashScreen({super.key});

  static const _brandBlue = Color(0xFF0F3D6E);

  @override
  Widget build(BuildContext context) {
    return const Scaffold(
      backgroundColor: Colors.white,
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Image(
              image: AssetImage('assets/icons/kawal_splash_logo.png'),
              width: 280,
              fit: BoxFit.contain,
            ),
            SizedBox(height: 40),
            SizedBox(
              width: 28,
              height: 28,
              child: CircularProgressIndicator(
                strokeWidth: 2.5,
                valueColor: AlwaysStoppedAnimation<Color>(_brandBlue),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
