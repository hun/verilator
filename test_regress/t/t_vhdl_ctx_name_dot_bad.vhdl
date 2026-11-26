-- DESCRIPTION: Verilator: VHDL context clause with a dot but no name after it
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A qualified context name needs a simple name after each dot.
-- The clause below ends right after the dot.

context work.;

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  o <= '0';
end architecture rtl;
