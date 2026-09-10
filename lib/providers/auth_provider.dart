import 'package:flutter/foundation.dart';

import '../models/app_user.dart';
import '../services/auth_service.dart';

enum AuthStatus { loading, signedOut, signedIn }

/// Login pengguna lewat backend FastAPI sendiri (JWT) — menggantikan
/// Firebase Auth + Firestore `users/{uid}` sepenuhnya. Beda dari versi
/// Firebase sebelumnya (yang bereaksi ke stream `authStateChanges`), di
/// sini tidak ada stream long-lived dari server - status dikelola langsung
/// lewat login()/logout()/restoreSession() sekali di awal.
class AuthProvider extends ChangeNotifier {
  final AuthService _authService;

  AuthProvider(this._authService) {
    _restoreSession();
  }

  AuthStatus status = AuthStatus.loading;
  AppUser? appUser;
  String? errorMessage;
  bool isBusy = false;

  bool get isAdmin => appUser?.isAdmin ?? false;

  /// Display name shown in the profile pill/avatar: the local part of the
  /// signed-in account's email (before the "@"), capitalized — e.g.
  /// "skripsi@tester.com" -> "Skripsi". Falls back to the account's stored
  /// name if the email is ever unavailable.
  String get displayName {
    final email = appUser?.email;
    if (email == null || email.isEmpty) return appUser?.name ?? '';
    final local = email.split('@').first;
    if (local.isEmpty) return local;
    return local[0].toUpperCase() + local.substring(1);
  }

  Future<void> _restoreSession() async {
    final user = await _authService.restoreSession();
    appUser = user;
    status = user != null ? AuthStatus.signedIn : AuthStatus.signedOut;
    notifyListeners();
  }

  Future<bool> login({required String email, required String password}) async {
    isBusy = true;
    errorMessage = null;
    notifyListeners();
    try {
      appUser = await _authService.login(email: email, password: password);
      status = AuthStatus.signedIn;
      return true;
    } on AuthException catch (e) {
      errorMessage = e.message;
      return false;
    } catch (e) {
      errorMessage = 'Gagal masuk: tidak bisa menghubungi server';
      return false;
    } finally {
      isBusy = false;
      notifyListeners();
    }
  }

  Future<void> logout() async {
    await _authService.logout();
    appUser = null;
    status = AuthStatus.signedOut;
    notifyListeners();
  }
}
