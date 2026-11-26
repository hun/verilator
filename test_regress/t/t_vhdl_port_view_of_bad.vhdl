-- DESCRIPTION: Verilator: VHDL array of mode views without an 'of' clause
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
--
-- An array of mode views must name its element subtype with an 'of' clause. The clause is missing after the view array.

entity t is
  port (p : view (vt));
end entity t;

architecture rtl of t is
begin
end architecture rtl;
