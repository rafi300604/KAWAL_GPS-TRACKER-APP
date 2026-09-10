import 'dart:convert';

import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';

import '../config/backend_config.dart';
import '../models/app_user.dart';

/// Login pengguna (email/password) lewat backend FastAPI sendiri
/// (`POST /auth/login`, JWT) — menggantikan Firebase Auth + Firestore
/// `users/{uid}` sepenuhnya. Token disimpan di SharedPreferences (sama
/// seperti mode tema), dilampirkan sebagai `Authorization: Bearer <token>`
/// di semua request user/waypoints berikutnya.
class AuthService {
  static const _tokenPrefsKey = 'auth_token';

  // Static (bukan instance field): DeviceService dibuat sebagai field
  // initializer di HomeScreen, sebelum context/Provider<AuthProvider>
  // tersedia, jadi tidak bisa disuntik instance AuthService yang sama lewat
  // constructor. Token disimpan di sini supaya DeviceService (lewat
  // AuthService.bearerHeader) selalu baca token TERKINI, siapa pun
  // instance AuthService yang menuliskannya (login/logout/restoreSession).
  static String? _token;

  /// Dipakai DeviceService untuk endpoint waypoints (butuh login user biasa,
  /// beda dari X-Admin-Key/X-API-Key statis yang dipakai endpoint device).
  static Map<String, String> get bearerHeader =>
      _token != null ? {'Authorization': 'Bearer $_token'} : {};

  Map<String, String> get _authHeaders => bearerHeader;

  Map<String, String> get _jsonHeaders => {
        'Content-Type': 'application/json',
        ..._authHeaders,
      };

  /// Dipanggil sekali saat app start: baca token tersimpan (kalau ada) dan
  /// validasi masih hidup lewat GET /users/me. Token yang sudah
  /// kedaluwarsa/tidak valid otomatis dibuang, bukan dibiarkan nyangkut.
  Future<AppUser?> restoreSession() async {
    final prefs = await SharedPreferences.getInstance();
    final saved = prefs.getString(_tokenPrefsKey);
    if (saved == null || saved.isEmpty) return null;
    _token = saved;
    try {
      final res = await http
          .get(Uri.parse('${BackendConfig.baseUrl}/users/me'), headers: _authHeaders)
          .timeout(const Duration(seconds: 10));
      if (res.statusCode == 200) {
        return AppUser.fromJson(jsonDecode(res.body) as Map<String, dynamic>);
      }
    } catch (_) {
      // Jaringan bermasalah sesaat - jangan buang token cuma karena satu
      // request gagal, biarkan user coba lagi (bukan dipaksa login ulang).
      return null;
    }
    // Status selain 200 (mis. 401 token kedaluwarsa) berarti token memang
    // tidak valid lagi - buang supaya tidak dicoba-coba lagi tiap start.
    _token = null;
    await prefs.remove(_tokenPrefsKey);
    return null;
  }

  Future<AppUser> login({required String email, required String password}) async {
    final res = await http
        .post(
          Uri.parse('${BackendConfig.baseUrl}/auth/login'),
          headers: const {'Content-Type': 'application/json'},
          body: jsonEncode({'email': email, 'password': password}),
        )
        .timeout(const Duration(seconds: 15));
    if (res.statusCode != 200) {
      String detail = 'Email atau kata sandi salah';
      try {
        detail = (jsonDecode(res.body) as Map<String, dynamic>)['detail'] as String? ?? detail;
      } catch (_) {
        // respons bukan JSON (mis. error jaringan/proxy) - pakai pesan default di atas
      }
      throw AuthException(detail);
    }
    final data = jsonDecode(res.body) as Map<String, dynamic>;
    _token = data['accessToken'] as String;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_tokenPrefsKey, _token!);
    return AppUser.fromJson(data['user'] as Map<String, dynamic>);
  }

  Future<void> logout() async {
    _token = null;
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_tokenPrefsKey);
  }

  Future<List<AppUser>> fetchAllUsers() async {
    final res = await http
        .get(Uri.parse('${BackendConfig.baseUrl}/users'), headers: _authHeaders)
        .timeout(const Duration(seconds: 10));
    if (res.statusCode != 200) {
      throw Exception('Gagal memuat pengguna (HTTP ${res.statusCode})');
    }
    final list = jsonDecode(res.body) as List;
    return list.map((e) => AppUser.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Admin-only: buat akun baru langsung di server (hash password server-side)
  /// -- jauh lebih simpel dari trik "secondary Firebase app" sebelumnya, karena
  /// admin memang tidak pernah kehilangan sesinya sendiri di sini.
  Future<AppUser> createUserAsAdmin({
    required String name,
    required String email,
    required String password,
    required String role,
  }) async {
    final res = await http
        .post(
          Uri.parse('${BackendConfig.baseUrl}/users'),
          headers: _jsonHeaders,
          body: jsonEncode({'name': name, 'email': email, 'password': password, 'role': role}),
        )
        .timeout(const Duration(seconds: 10));
    if (res.statusCode != 200) {
      throw Exception(_extractDetail(res.body, 'Gagal membuat pengguna (HTTP ${res.statusCode})'));
    }
    return AppUser.fromJson(jsonDecode(res.body) as Map<String, dynamic>);
  }

  Future<AppUser> updateUserAsAdmin({
    required AppUser existing,
    required String name,
    required String role,
  }) async {
    final res = await http
        .patch(
          Uri.parse('${BackendConfig.baseUrl}/users/${existing.id}'),
          headers: _jsonHeaders,
          body: jsonEncode({'name': name, 'role': role}),
        )
        .timeout(const Duration(seconds: 10));
    if (res.statusCode != 200) {
      throw Exception(_extractDetail(res.body, 'Gagal memperbarui (HTTP ${res.statusCode})'));
    }
    return AppUser.fromJson(jsonDecode(res.body) as Map<String, dynamic>);
  }

  Future<AppUser> updateOwnName({required AppUser existing, required String name}) async {
    final res = await http
        .patch(
          Uri.parse('${BackendConfig.baseUrl}/users/me'),
          headers: _jsonHeaders,
          body: jsonEncode({'name': name}),
        )
        .timeout(const Duration(seconds: 10));
    if (res.statusCode != 200) {
      throw Exception(_extractDetail(res.body, 'Gagal memperbarui (HTTP ${res.statusCode})'));
    }
    return AppUser.fromJson(jsonDecode(res.body) as Map<String, dynamic>);
  }

  String _extractDetail(String body, String fallback) {
    try {
      return (jsonDecode(body) as Map<String, dynamic>)['detail'] as String? ?? fallback;
    } catch (_) {
      return fallback;
    }
  }
}

class AuthException implements Exception {
  final String message;
  const AuthException(this.message);
  @override
  String toString() => message;
}
