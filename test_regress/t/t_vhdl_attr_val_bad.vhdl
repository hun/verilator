-- DESCRIPTION: Verilator: VHDL enum val attribute bounds
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (y : out bit);
end;
architecture rtl of t is
  type op is (add, sub);
  signal s : op;
begin
  s <= op'val(5);
  y <= '0';
end;
