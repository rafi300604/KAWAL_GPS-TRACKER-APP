import '../config/backend_config.dart';

class AppUser {
  final String id;
  final String name;
  final String email;
  final String role; // 'admin' | 'user'
  final DateTime createdAt;

  const AppUser({
    required this.id,
    required this.name,
    required this.email,
    required this.role,
    required this.createdAt,
  });

  bool get isAdmin => role == 'admin';

  /// Dari respons backend FastAPI (`POST /auth/login`, `GET /users`,
  /// `GET /users/me`) — lihat `backend/app/schemas.py:UserOut`.
  factory AppUser.fromJson(Map<String, dynamic> json) {
    return AppUser(
      id: json['id'].toString(),
      name: json['name'] as String? ?? '',
      email: json['email'] as String? ?? '',
      role: json['role'] as String? ?? 'user',
      createdAt: json['createdAt'] != null
          ? parseBackendUtc(json['createdAt'] as String)
          : DateTime.now(),
    );
  }
}
