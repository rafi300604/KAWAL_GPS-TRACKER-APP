import 'dart:async';

import 'package:firebase_auth/firebase_auth.dart' as fb;
import 'package:flutter/foundation.dart';

import '../models/app_user.dart';
import '../services/auth_service.dart';

enum AuthStatus { loading, signedOut, signedIn }

/// Combines Firebase Auth's signed-in state with the matching Firestore
/// `users/{uid}` profile (which carries the app role) into one stream of
/// truth for the UI.
class AuthProvider extends ChangeNotifier {
  final AuthService _authService;

  AuthProvider(this._authService) {
    _authSub = _authService.authStateChanges.listen(_onAuthChanged);
  }

  StreamSubscription<fb.User?>? _authSub;
  StreamSubscription<AppUser?>? _profileSub;

  AuthStatus status = AuthStatus.loading;
  fb.User? firebaseUser;
  AppUser? appUser;
  String? errorMessage;
  bool isBusy = false;

  bool get isAdmin => appUser?.isAdmin ?? false;

  /// Display name shown in the profile pill/avatar: the local part of the
  /// signed-in Firebase Auth email (before the "@"), capitalized — e.g.
  /// "skripsi@tester.com" -> "Skripsi". Falls back to the Firestore profile
  /// name if the email is ever unavailable.
  String get displayName {
    final email = firebaseUser?.email;
    if (email == null || email.isEmpty) return appUser?.name ?? '';
    final local = email.split('@').first;
    if (local.isEmpty) return local;
    return local[0].toUpperCase() + local.substring(1);
  }

  void _onAuthChanged(fb.User? user) {
    firebaseUser = user;
    _profileSub?.cancel();

    if (user == null) {
      appUser = null;
      status = AuthStatus.signedOut;
      notifyListeners();
      return;
    }

    status = AuthStatus.loading;
    notifyListeners();
    _profileSub = _authService.watchAppUser(user.uid).listen((profile) {
      appUser = profile;
      status = AuthStatus.signedIn;
      notifyListeners();
    });
  }

  Future<bool> login({required String email, required String password}) async {
    isBusy = true;
    errorMessage = null;
    notifyListeners();
    try {
      await _authService.signIn(email: email, password: password);
      return true;
    } on fb.FirebaseAuthException catch (e) {
      errorMessage = _friendlyAuthError(e.code);
      return false;
    } finally {
      isBusy = false;
      notifyListeners();
    }
  }

  Future<void> logout() => _authService.signOut();

  String _friendlyAuthError(String code) {
    switch (code) {
      case 'user-not-found':
      case 'wrong-password':
      case 'invalid-credential':
        return 'Email atau kata sandi salah';
      case 'invalid-email':
        return 'Format email tidak valid';
      case 'user-disabled':
        return 'Akun ini telah dinonaktifkan';
      case 'too-many-requests':
        return 'Terlalu banyak percobaan, coba lagi nanti';
      default:
        return 'Gagal masuk: $code';
    }
  }

  @override
  void dispose() {
    _authSub?.cancel();
    _profileSub?.cancel();
    super.dispose();
  }
}
